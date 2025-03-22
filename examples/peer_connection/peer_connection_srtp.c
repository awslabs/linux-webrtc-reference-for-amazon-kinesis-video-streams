#include <stdlib.h>
#include "logging.h"
#include "peer_connection.h"
#include "peer_connection_srtp.h"
#include "peer_connection_rolling_buffer.h"
#include "peer_connection_jitter_buffer.h"
#include "metric.h"

/* API includes. */
#include "rtp_api.h"
#include "rtcp_api.h"
#include "peer_connection_g711_helper.h"
#include "peer_connection_h264_helper.h"
#include "peer_connection_h265_helper.h"
#include "peer_connection_opus_helper.h"
#include "ice_controller.h"

/* At write frame, we reserve 2 bytes at the beginning of payload buffer for re-transmission if RTX is enabled. */
/* The format of a retransmission packet is shown below:
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                         RTP Header                            |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            OSN                |                               |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
 |                  Original RTP Packet Payload                  |
 |                                                               |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define PEER_CONNECTION_SRTP_RTX_WRITE_RESERVED_BYTES ( 2 )

#define PEER_CONNECTION_SRTP_H264_MAX_NALUS_IN_A_FRAME        ( 64 )
#define PEER_CONNECTION_SRTP_H265_MAX_NALUS_IN_A_FRAME        ( 64 )                         
#define PEER_CONNECTION_SRTP_RTP_PAYLOAD_MAX_LENGTH      ( 1200 )

#define PEER_CONNECTION_SRTP_VIDEO_CLOCKRATE ( uint32_t ) 90000
#define PEER_CONNECTION_SRTP_OPUS_CLOCKRATE  ( uint32_t ) 48000
#define PEER_CONNECTION_SRTP_PCM_CLOCKRATE   ( uint32_t ) 8000

#define PEER_CONNECTION_SRTP_US_IN_A_SECOND ( 1000000 )
#define PEER_CONNECTION_SRTP_CONVERT_TIME_US_TO_RTP_TIMESTAMP( clockRate, presentationUs ) ( uint32_t )( ( ( ( presentationUs ) * ( clockRate ) ) / PEER_CONNECTION_SRTP_US_IN_A_SECOND ) & 0xFFFFFFFF )
#define PEER_CONNECTION_SRTP_CONVERT_RTP_TIMESTAMP_TO_TIME_US( clockRate, rtpTimestamp ) ( ( uint64_t )( rtpTimestamp ) * PEER_CONNECTION_SRTP_US_IN_A_SECOND / ( clockRate ) )

#define PEER_CONNECTION_SRTP_JITTER_BUFFER_TOLERENCE_TIME_SECOND ( 2 )

/*
 *
     0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |       0xBE    |    0xDE       |           length=1            |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |  ID   | L=1   |transport-wide sequence number | zero padding  |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
// https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
#define PEER_CONNECTION_SRTP_TWCC_EXT_PROFILE ( 0xBEDE )
#define PEER_CONNECTION_SRTP_GET_TWCC_PAYLOAD( extId, sequenceNum ) ( ( ( ( extId ) & 0xfu ) << 28u ) | ( 1u << 24u ) | ( ( uint32_t ) ( sequenceNum ) << 8u ) )

#define PEER_CONNECTION_SRTCP_NACK_MAX_SEQ_NUM ( 128 )

static PeerConnectionResult_t OnJitterBufferFrameReady( void * pCustomContext,
                                                        uint16_t startSequence,
                                                        uint16_t endSequence )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK, retFillFrame = PEER_CONNECTION_RESULT_OK;
    PeerConnectionSrtpReceiver_t * pSrtpReceiver = NULL;
    size_t frameBufferLength = PEER_CONNECTION_FRAME_BUFFER_SIZE;
    PeerConnectionFrame_t frame;
    uint32_t rtpTimestamp;

    if( pCustomContext == NULL )
    {
        LogError( ( "Invalid input, pCustomContext: %p", pCustomContext ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSrtpReceiver = ( PeerConnectionSrtpReceiver_t * ) pCustomContext;

        /* Return fail only when hitting critical issues. If fill fram API returns fail, we still return
         * OK to the jitter buffer to release these packet normally. */
        retFillFrame = PeerConnectionJitterBuffer_FillFrame( &pSrtpReceiver->rxJitterBuffer,
                                                             startSequence,
                                                             endSequence,
                                                             pSrtpReceiver->frameBuffer,
                                                             &frameBufferLength,
                                                             &rtpTimestamp );
        LogDebug( ( "Fill frame with result: %d, length: %lu, start seq: %u, end seq: %u",
                    retFillFrame,
                    frameBufferLength,
                    startSequence,
                    endSequence ) );
    }

    if( retFillFrame == PEER_CONNECTION_RESULT_OK )
    {
        if( pSrtpReceiver->onFrameReadyCallbackFunc )
        {
            memset( &frame, 0, sizeof( PeerConnectionFrame_t ) );
            frame.version = PEER_CONNECTION_FRAME_CURRENT_VERSION;
            frame.presentationUs = PEER_CONNECTION_SRTP_CONVERT_RTP_TIMESTAMP_TO_TIME_US( pSrtpReceiver->rxJitterBuffer.clockRate, rtpTimestamp );
            frame.pData = pSrtpReceiver->frameBuffer;
            frame.dataLength = frameBufferLength;
            pSrtpReceiver->onFrameReadyCallbackFunc( pSrtpReceiver->pOnFrameReadyCallbackCustomContext, &frame );
        }
    }

    return ret;
}

static PeerConnectionResult_t OnJitterBufferFrameDrop( void * pCustomContext,
                                                       uint16_t startSequence,
                                                       uint16_t endSequence )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    return ret;
}

PeerConnectionResult_t PeerConnectionSrtp_ConstructSrtpPacket( PeerConnectionSession_t * pSession,
                                                               RtpPacket_t * pPacketRtp,
                                                               uint8_t * pOutputSrtpPacket,
                                                               size_t * pOutputSrtpPacketLength )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    RtpResult_t resultRtp;
    size_t rtpBufferLength;
    srtp_err_status_t errorStatus;

    if( ( pSession == NULL ) ||
        ( pPacketRtp == NULL ) ||
        ( pOutputSrtpPacket == NULL ) ||
        ( pOutputSrtpPacketLength == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pPacketRtp: %p, pOutputSrtpPacket: %p, pOutputSrtpPacketLength: %p",
                    pSession,
                    pPacketRtp,
                    pOutputSrtpPacket,
                    pOutputSrtpPacketLength ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    /* Get buffer from sender for serializing RTP packet */
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        rtpBufferLength = *pOutputSrtpPacketLength;
    }

    /* Contruct RTP packet for each payload buffer. */
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultRtp = Rtp_Serialize( &pSession->pCtx->rtpContext,
                                   pPacketRtp,
                                   pOutputSrtpPacket,
                                   &rtpBufferLength );
        if( resultRtp != RTP_RESULT_OK )
        {
            LogError( ( "Fail to serialize RTP packet, result: %d", resultRtp ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTP_SERIALIZE;
        }
    }

    /* Encrypt it by SRTP. */
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        errorStatus = srtp_protect( pSession->srtpTransmitSession, pOutputSrtpPacket, rtpBufferLength, pOutputSrtpPacket, pOutputSrtpPacketLength, 0 );
        if( errorStatus != srtp_err_status_ok )
        {
            LogError( ( "Fail to encrypt Tx SRTP packet, errorStatus: %d", errorStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ENCRYPT_SRTP_RTP_PACKET;
        }
    }

    return ret;
}

static PeerConnectionResult_t ResendSrtpPacket( PeerConnectionSession_t * pSession,
                                                const Transceiver_t * pTransceiver,
                                                uint16_t rtpSeq,
                                                uint32_t ssrc )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    PeerConnectionSrtpSender_t * pSrtpSender = NULL;
    uint8_t isLocked = 0;
    PeerConnectionRollingBufferPacket_t * pRollingBufferPacket = NULL;
    IceControllerResult_t resultIceController;
    uint8_t bufferAfterEncrypt = 1;
    uint8_t srtpBuffer[ PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH ];
    uint8_t * pSrtpPacket = NULL;
    size_t srtpPacketLength = 0;
    uint32_t payloadType;
    uint16_t * pRtpSeq = NULL;
    uint16_t * pOsn = NULL;

    if( ( pSession == NULL ) || ( pTransceiver == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pTransceiver: %p", pSession, pTransceiver ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pRtpSeq = &rtpSeq;
        if( pTransceiver->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO )
        {
            pSrtpSender = &pSession->videoSrtpSender;
            payloadType = pSession->rtpConfig.videoCodecPayload;
            if( ( pSession->rtpConfig.videoCodecRtxPayload != 0 ) &&
                ( pSession->rtpConfig.videoCodecRtxPayload != pSession->rtpConfig.videoCodecPayload ) )
            {
                /* Use RTX payload type, sequence number and ssrc for re-transmission. */
                bufferAfterEncrypt = 0;
                payloadType = pSession->rtpConfig.videoCodecRtxPayload;
                pRtpSeq = &pSession->rtpConfig.videoRtxSequenceNumber;
                ssrc = pTransceiver->rtxSsrc;
            }
        }
        else
        {
            pSrtpSender = &pSession->audioSrtpSender;
            payloadType = pSession->rtpConfig.audioCodecPayload;
            if( ( pSession->rtpConfig.audioCodecRtxPayload != 0 ) &&
                ( pSession->rtpConfig.audioCodecRtxPayload != pSession->rtpConfig.audioCodecPayload ) )
            {
                /* Use RTX payload type, sequence number and ssrc for re-transmission. */
                bufferAfterEncrypt = 0;
                payloadType = pSession->rtpConfig.audioCodecRtxPayload;
                pRtpSeq = &pSession->rtpConfig.audioRtxSequenceNumber;
                ssrc = pTransceiver->rtxSsrc;
            }
        }

        /* Lock sender. */
        if( pthread_mutex_lock( &( pSrtpSender->senderMutex ) ) == 0 )
        {
            isLocked = 1;
        }
        else
        {
            LogError( ( "Fail to take sender mutex" ) );
            ret = PEER_CONNECTION_RESULT_FAIL_TAKE_SENDER_MUTEX;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = PeerConnectionRollingBuffer_SearchRtpSequenceBuffer( &pSrtpSender->txRollingBuffer,
                                                                   rtpSeq,
                                                                   &pRollingBufferPacket );
        if( ( ret != PEER_CONNECTION_RESULT_OK ) || ( pRollingBufferPacket == NULL ) )
        {
            LogWarn( ( "Fail to find target buffer, seq: %u", rtpSeq ) );
        }
        else
        {
            LogDebug( ( "Found target buffer, pRollingBufferPacket: %p, packetBufferLength: %lu, pPacketBuffer: %p",
                        pRollingBufferPacket,
                        pRollingBufferPacket->packetBufferLength,
                        pRollingBufferPacket->pPacketBuffer ) );
            LogDebug( ( "Found target buffer, sequence in buffer: %u, target sequence: %u",
                        pRollingBufferPacket->rtpPacket.header.sequenceNumber, rtpSeq ) );
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        if( bufferAfterEncrypt == 0 )
        {
            /* Don't reset the header as re-using the setting from write frame.
             * Update sequence, SSRC, payload type and OSN for RTX packet. */
            pRollingBufferPacket->rtpPacket.header.sequenceNumber = ( *pRtpSeq )++;
            pRollingBufferPacket->rtpPacket.header.ssrc = ssrc;
            pRollingBufferPacket->rtpPacket.header.payloadType = payloadType;

            /* Follow RTX format to add OSN(original RTP sequence number) at the very beginning of payload.
             * Note that we reserve PEER_CONNECTION_SRTP_RTX_WRITE_RESERVED_BYTES at the beginning of buffer at write frame. */
            pOsn = ( uint16_t * ) pRollingBufferPacket->pPacketBuffer;
            *pOsn = htons( rtpSeq );
            pRollingBufferPacket->rtpPacket.payloadLength = pRollingBufferPacket->packetBufferLength + 2;
            pRollingBufferPacket->rtpPacket.pPayload = pRollingBufferPacket->pPacketBuffer;

            pSrtpPacket = srtpBuffer;
            srtpPacketLength = PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH;
            /* ConstructSrtpPacket() serializes RTP packet and encrypt it. */
            ret = PeerConnectionSrtp_ConstructSrtpPacket( pSession,
                                                          &pRollingBufferPacket->rtpPacket,
                                                          pSrtpPacket,
                                                          &srtpPacketLength );
        }
        else
        {
            pSrtpPacket = pRollingBufferPacket->pPacketBuffer;
            srtpPacketLength = pRollingBufferPacket->packetBufferLength;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultIceController = IceController_SendToRemotePeer( &pSession->iceControllerContext,
                                                              pSrtpPacket,
                                                              srtpPacketLength );
        if( resultIceController != ICE_CONTROLLER_RESULT_OK )
        {
            LogWarn( ( "Fail to re-send RTP packet, ret: %d, seq: %u, SSRC: 0x%x", resultIceController, rtpSeq, ssrc ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_RESEND_RTP_PACKET;
        }
        else
        {
            LogDebug( ( "Re-send RTP successfully, RTP seq: %u, SSRC: 0x%x", rtpSeq, ssrc ) );
        }
    }

    if( isLocked )
    {
        pthread_mutex_unlock( &( pSrtpSender->senderMutex ) );
    }

    return ret;
}

static PeerConnectionResult_t OnRtcpNackEvent( PeerConnectionSession_t * pSession,
                                               RtcpPacket_t * pRtcpPacket )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    RtcpResult_t resultRtcp;
    RtcpNackPacket_t nackPacket;
    const Transceiver_t * pTransceiver = NULL;
    uint16_t seqNumList[ PEER_CONNECTION_SRTCP_NACK_MAX_SEQ_NUM ];
    int i;

    if( ( pSession == NULL ) || ( pRtcpPacket == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pRtcpPacket: %p", pSession, pRtcpPacket ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        memset( &nackPacket, 0, sizeof( RtcpNackPacket_t ) );
        memset( &seqNumList, 0, sizeof( seqNumList ) );
        nackPacket.pSeqNumList = seqNumList;
        nackPacket.seqNumListLength = PEER_CONNECTION_SRTCP_NACK_MAX_SEQ_NUM;
        resultRtcp = Rtcp_ParseNackPacket( &pSession->pCtx->rtcpContext,
                                           pRtcpPacket,
                                           &nackPacket );
        if( resultRtcp != RTCP_RESULT_OK )
        {
            LogError( ( "Fail to parse RTCP NACK packet, result: %d", resultRtcp ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTCP_PARSE_NACK;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = PeerConnection_MatchTransceiverBySsrc( pSession,
                                                     nackPacket.mediaSourceSsrc,
                                                     &pTransceiver );
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        for( i = 0; i < nackPacket.seqNumListLength; i++ )
        {
            /* Retransmit matching sequence number one by one. */
            ret = ResendSrtpPacket( pSession, pTransceiver, nackPacket.pSeqNumList[i], nackPacket.senderSsrc );
            if( ret != PEER_CONNECTION_RESULT_OK )
            {
                break;
            }
        }
    }

    return ret;
}

PeerConnectionResult_t PeerConnectionSrtp_Init( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    srtp_policy_t receivePolicy, transmitPolicy;
    void (* srtp_policy_setter)( srtp_crypto_policy_t * ) = NULL;
    void (* srtcp_policy_setter)( srtp_crypto_policy_t * ) = NULL;
    srtp_err_status_t errorStatus;
    PeerConnectionSrtpSender_t * pSrtpSender = NULL;
    PeerConnectionSrtpReceiver_t * pSrtpReceiver = NULL;
    int i;
    size_t maxSizePerPacket = PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        switch( pSession->dtlsSession.xNetworkCredentials.dtlsKeyingMaterial.srtpProfile )
        {
            case KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_80:
                srtp_policy_setter = srtp_crypto_policy_set_rtp_default;
                srtcp_policy_setter = srtp_crypto_policy_set_rtp_default;
                break;
            case KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_32:
                srtp_policy_setter = srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32;
                srtcp_policy_setter = srtp_crypto_policy_set_rtp_default;
                break;
            default:
                LogError( ( "Unknown SRTP profile: %d", pSession->dtlsSession.xNetworkCredentials.dtlsKeyingMaterial.srtpProfile ) );
                ret = PEER_CONNECTION_RESULT_UNKNOWN_SRTP_PROFILE;
                break;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        memset( &receivePolicy, 0, sizeof( receivePolicy ) );
        srtp_policy_setter( &receivePolicy.rtp );
        srtcp_policy_setter( &receivePolicy.rtcp );

        receivePolicy.key = pSession->dtlsSession.xNetworkCredentials.dtlsKeyingMaterial.serverWriteKey;
        receivePolicy.ssrc.type = ssrc_any_inbound;
        receivePolicy.next = NULL;

        errorStatus = srtp_create( &( pSession->srtpReceiveSession ), &receivePolicy );
        if( errorStatus != srtp_err_status_ok )
        {
            LogError( ( "Fail to create Rx SRTP session, errorStatus: %d", errorStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_CREATE_SRTP_RX_SESSION;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        memset( &transmitPolicy, 0, sizeof( transmitPolicy ) );
        srtp_policy_setter( &transmitPolicy.rtp );
        srtcp_policy_setter( &transmitPolicy.rtcp );

        transmitPolicy.key = pSession->dtlsSession.xNetworkCredentials.dtlsKeyingMaterial.clientWriteKey;
        transmitPolicy.ssrc.type = ssrc_any_outbound;
        transmitPolicy.next = NULL;

        errorStatus = srtp_create( &( pSession->srtpTransmitSession ), &transmitPolicy );
        if( errorStatus != srtp_err_status_ok )
        {
            LogError( ( "Fail to create Tx SRTP session, errorStatus: %d", errorStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_CREATE_SRTP_TX_SESSION;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Initialize Rolling buffers. */
        for( i = 0; i < pSession->transceiverCount; i++ )
        {
            if( ( pSession->pTransceivers[i]->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO ) &&
                ( ( pSession->pTransceivers[i]->direction == TRANSCEIVER_TRACK_DIRECTION_SENDRECV ) ||
                  ( pSession->pTransceivers[i]->direction == TRANSCEIVER_TRACK_DIRECTION_SENDONLY ) ) )
            {
                pSrtpSender = &pSession->videoSrtpSender;
                if( ( pSession->rtpConfig.videoCodecRtxPayload != 0 ) &&
                    ( pSession->rtpConfig.videoCodecRtxPayload != pSession->rtpConfig.videoCodecPayload ) )
                {
                    /* If we're using different payload type in re-transmission, we create the rolling buffer just for RTP payload. */
                    maxSizePerPacket = PEER_CONNECTION_SRTP_RTP_PAYLOAD_MAX_LENGTH;
                }
                ret = PeerConnectionRollingBuffer_Create( &pSession->videoSrtpSender.txRollingBuffer,
                                                          pSession->pTransceivers[i]->rollingbufferBitRate, // bps
                                                          pSession->pTransceivers[i]->rollingbufferDurationSec, // duration in seconds
                                                          maxSizePerPacket );
            }
            else if( ( pSession->pTransceivers[i]->trackKind == TRANSCEIVER_TRACK_KIND_AUDIO ) &&
                     ( ( pSession->pTransceivers[i]->direction == TRANSCEIVER_TRACK_DIRECTION_SENDRECV ) ||
                       ( pSession->pTransceivers[i]->direction == TRANSCEIVER_TRACK_DIRECTION_SENDONLY ) ) )
            {
                pSrtpSender = &pSession->audioSrtpSender;
                if( ( pSession->rtpConfig.audioCodecRtxPayload != 0 ) &&
                    ( pSession->rtpConfig.audioCodecRtxPayload != pSession->rtpConfig.audioCodecPayload ) )
                {
                    /* If we're using different payload type in re-transmission, we create the rolling buffer just for RTP payload. */
                    maxSizePerPacket = PEER_CONNECTION_SRTP_RTP_PAYLOAD_MAX_LENGTH;
                }
                ret = PeerConnectionRollingBuffer_Create( &pSession->audioSrtpSender.txRollingBuffer,
                                                          pSession->pTransceivers[i]->rollingbufferBitRate, // bps
                                                          pSession->pTransceivers[i]->rollingbufferDurationSec, // duration in seconds
                                                          maxSizePerPacket );
            }
            else
            {
                LogInfo( ( "No send needed for this transceiver, kind: %d, direction: %d",
                           pSession->pTransceivers[i]->trackKind,
                           pSession->pTransceivers[i]->direction ) );
            }

            if( ret != PEER_CONNECTION_RESULT_OK )
            {
                break;
            }

            /* Mutex can only be created in executing scheduler. */
            if( pthread_mutex_init( &( pSrtpSender->senderMutex ), NULL ) != 0 )
            {
                LogError( ( "Fail to create mutex for SRTP sender." ) );
                ret = PEER_CONNECTION_RESULT_FAIL_CREATE_SENDER_MUTEX;
                break;
            }
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Initialize Jitter buffers. */
        for( i = 0; i < pSession->transceiverCount; i++ )
        {
            if( ( pSession->pTransceivers[i]->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO ) &&
                ( ( pSession->pTransceivers[i]->direction == TRANSCEIVER_TRACK_DIRECTION_SENDRECV ) ||
                  ( pSession->pTransceivers[i]->direction == TRANSCEIVER_TRACK_DIRECTION_RECVONLY ) ) )
            {
                LogInfo( ( "Setting video receiver." ) );
                pSrtpReceiver = &pSession->videoSrtpReceiver;
                ret = PeerConnectionJitterBuffer_Create( &pSrtpReceiver->rxJitterBuffer,
                                                         OnJitterBufferFrameReady,
                                                         pSrtpReceiver,
                                                         OnJitterBufferFrameDrop,
                                                         pSrtpReceiver,
                                                         PEER_CONNECTION_SRTP_JITTER_BUFFER_TOLERENCE_TIME_SECOND,   // buffer time in seconds
                                                         pSession->pTransceivers[i]->codecBitMap,
                                                         PEER_CONNECTION_SRTP_VIDEO_CLOCKRATE );
            }
            else if( ( pSession->pTransceivers[i]->trackKind == TRANSCEIVER_TRACK_KIND_AUDIO ) &&
                     ( ( pSession->pTransceivers[i]->direction == TRANSCEIVER_TRACK_DIRECTION_SENDRECV ) ||
                       ( pSession->pTransceivers[i]->direction == TRANSCEIVER_TRACK_DIRECTION_RECVONLY ) ) )
            {
                LogInfo( ( "Setting audio receiver." ) );
                pSrtpReceiver = &pSession->audioSrtpReceiver;
                if( ( pSession->rtpConfig.audioCodecRtxPayload != 0 ) &&
                    ( pSession->rtpConfig.audioCodecRtxPayload != pSession->rtpConfig.audioCodecPayload ) )
                {
                    /* If we're using different payload type in re-transmission, we create the rolling buffer just for RTP payload. */
                    maxSizePerPacket = PEER_CONNECTION_SRTP_RTP_PAYLOAD_MAX_LENGTH;
                }
                ret = PeerConnectionJitterBuffer_Create( &pSrtpReceiver->rxJitterBuffer,
                                                         OnJitterBufferFrameReady,
                                                         pSrtpReceiver,
                                                         OnJitterBufferFrameDrop,
                                                         pSrtpReceiver,
                                                         PEER_CONNECTION_SRTP_JITTER_BUFFER_TOLERENCE_TIME_SECOND,   // buffer time in seconds
                                                         pSession->pTransceivers[i]->codecBitMap,
                                                         PEER_CONNECTION_SRTP_PCM_CLOCKRATE );
            }
            else
            {
                LogInfo( ( "No recv needed for this transceiver, kind: %d, direction: %d",
                           pSession->pTransceivers[i]->trackKind,
                           pSession->pTransceivers[i]->direction ) );
            }

            if( ret != PEER_CONNECTION_RESULT_OK )
            {
                break;
            }
        }
    }

    return ret;
}

PeerConnectionResult_t PeerConnectionSrtp_DeInit( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    srtp_err_status_t errorStatus;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        return PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    /* Clean up SRTP sessions */
    if( pSession->srtpReceiveSession != NULL )
    {
        errorStatus = srtp_dealloc( pSession->srtpReceiveSession );
        if( errorStatus != srtp_err_status_ok )
        {
            LogError( ( "Fail to deallocate Rx SRTP session, errorStatus: %d", errorStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_DECRYPT_SRTP_RTP_PACKET;
        }
        pSession->srtpReceiveSession = NULL;
    }

    if( pSession->srtpTransmitSession != NULL )
    {
        errorStatus = srtp_dealloc( pSession->srtpTransmitSession );
        if( errorStatus != srtp_err_status_ok )
        {
            LogError( ( "Fail to deallocate Tx SRTP session, errorStatus: %d", errorStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ENCRYPT_SRTP_RTP_PACKET;
        }
        pSession->srtpTransmitSession = NULL;
    }

    /* Clean up Video SRTP Sender */
    if( pthread_mutex_lock( &( pSession->videoSrtpSender.senderMutex ) ) == 0 )
    {
        PeerConnectionRollingBuffer_Free( &pSession->videoSrtpSender.txRollingBuffer );
        pthread_mutex_unlock( &( pSession->videoSrtpSender.senderMutex ) );
        pthread_mutex_destroy( &( pSession->videoSrtpSender.senderMutex ) );
    }

    /* Clean up Audio SRTP Sender */
    if( pthread_mutex_lock( &( pSession->audioSrtpSender.senderMutex ) ) == 0 )
    {
        PeerConnectionRollingBuffer_Free( &pSession->audioSrtpSender.txRollingBuffer );
        pthread_mutex_unlock( &( pSession->audioSrtpSender.senderMutex ) );
        pthread_mutex_destroy( &( pSession->audioSrtpSender.senderMutex ) );
    }

    /* Clean up Video SRTP Receiver */
    memset( pSession->videoSrtpReceiver.frameBuffer, 0, PEER_CONNECTION_FRAME_BUFFER_SIZE );

    PeerConnectionJitterBuffer_Free( &pSession->videoSrtpReceiver.rxJitterBuffer );

    /* Clean up Audio SRTP Receiver */
    memset( pSession->audioSrtpReceiver.frameBuffer, 0, PEER_CONNECTION_FRAME_BUFFER_SIZE );

    PeerConnectionJitterBuffer_Free( &pSession->audioSrtpReceiver.rxJitterBuffer );

    /* Reset callback functions */
    pSession->videoSrtpReceiver.onFrameReadyCallbackFunc = NULL;
    pSession->videoSrtpReceiver.pOnFrameReadyCallbackCustomContext = NULL;
    pSession->audioSrtpReceiver.onFrameReadyCallbackFunc = NULL;
    pSession->audioSrtpReceiver.pOnFrameReadyCallbackCustomContext = NULL;

    return ret;
}

PeerConnectionResult_t PeerConnectionSrtp_HandleSrtpPacket( PeerConnectionSession_t * pSession,
                                                            uint8_t * pBuffer,
                                                            size_t bufferLength )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    srtp_err_status_t errorStatus;
    uint8_t rtpBuffer[ PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH ];
    size_t rtpBufferLength = PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH;
    RtpResult_t resultRtp;
    RtpPacket_t rtpPacket;
    PeerConnectionJitterBufferPacket_t * pJitterBufferPacket = NULL;
    PeerConnectionSrtpReceiver_t * pSrtpReceiver = NULL;

    if( ( pSession == NULL ) || ( pBuffer == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pBuffer: %p", pSession, pBuffer ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        errorStatus = srtp_unprotect( pSession->srtpReceiveSession,
                                      pBuffer,
                                      bufferLength,
                                      rtpBuffer,
                                      &rtpBufferLength );
        if( errorStatus != srtp_err_status_ok )
        {
            LogError( ( "Fail to decrypt Rx SRTP packet, errorStatus: %d", errorStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_DECRYPT_SRTP_RTP_PACKET;
        }
        else
        {
            LogVerbose( ( "Decrypt SRTP packet successfully, decrypted length: %lu", rtpBufferLength ) );
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Deserialize RTP packet. */
        resultRtp = Rtp_DeSerialize( &pSession->pCtx->rtpContext,
                                     rtpBuffer,
                                     rtpBufferLength,
                                     &rtpPacket );
        if( resultRtp != RTP_RESULT_OK )
        {
            LogError( ( "Fail to deserialize RTP packet, result: %d", resultRtp ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTP_DESERIALIZE;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        if( pSession->rtpConfig.remoteVideoSsrc == rtpPacket.header.ssrc )
        {
            pSrtpReceiver = &pSession->videoSrtpReceiver;
        }
        else if( pSession->rtpConfig.remoteAudioSsrc == rtpPacket.header.ssrc )
        {
            pSrtpReceiver = &pSession->audioSrtpReceiver;
        }
        else
        {
            LogWarn( ( "Received unknown SSRC: %u RTP packet.", rtpPacket.header.ssrc ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTP_RX_NO_MATCHING_SSRC;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = PeerConnectionJitterBuffer_AllocateBuffer( &pSrtpReceiver->rxJitterBuffer,
                                                         &pJitterBufferPacket,
                                                         rtpPacket.payloadLength,
                                                         rtpPacket.header.sequenceNumber );
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        memcpy( pJitterBufferPacket->pPacketBuffer, rtpPacket.pPayload, rtpPacket.payloadLength );
        pJitterBufferPacket->receiveTick = time( NULL ); //xTaskGetTickCount();
        pJitterBufferPacket->rtpTimestamp = rtpPacket.header.timestamp;
        pJitterBufferPacket->sequenceNumber = rtpPacket.header.sequenceNumber;
        // LogInfo( ( "Dumping RTP payload: %u, seq: %u, timestamp: %lu", rtpPacket.payloadLength, rtpPacket.header.sequenceNumber, rtpPacket.header.timestamp ) );
        // for( int i = 0; i < rtpPacket.payloadLength; i++ )
        // {
        //     printf( "%02x ", rtpPacket.pPayload[i] );
        // }
        // printf( "\n" );

        ret = PeerConnectionJitterBuffer_Push( &pSrtpReceiver->rxJitterBuffer,
                                               pJitterBufferPacket );
    }

    return ret;
}

PeerConnectionResult_t PeerConnectionSrtp_HandleSrtcpPacket( PeerConnectionSession_t * pSession,
                                                             uint8_t * pBuffer,
                                                             size_t bufferLength )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    srtp_err_status_t errorStatus;
    static uint8_t rtcpBuffer[ PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH ];
    size_t rtcpBufferLength = PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH;
    RtcpResult_t resultRtcp;
    RtcpPacket_t rtcpPacket;

    if( ( pSession == NULL ) || ( pBuffer == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pBuffer: %p", pSession, pBuffer ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        errorStatus = srtp_unprotect_rtcp( pSession->srtpReceiveSession,
                                           pBuffer,
                                           bufferLength,
                                           rtcpBuffer,
                                           &rtcpBufferLength );
        if( errorStatus != srtp_err_status_ok )
        {
            LogError( ( "Fail to decrypt Rx SRTCP packet, errorStatus: %d", errorStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_DECRYPT_SRTP_RTP_PACKET;
        }
        else
        {
            LogVerbose( ( "Decrypt SRTCP packet successfully, decrypted length: %lu", rtcpBufferLength ) );
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultRtcp = Rtcp_DeserializePacket( &pSession->pCtx->rtcpContext,
                                             rtcpBuffer,
                                             rtcpBufferLength,
                                             &rtcpPacket );
        if( resultRtcp != RTCP_RESULT_OK )
        {
            LogError( ( "Fail to deserialize RTCP packet, result: %d", resultRtcp ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTCP_DESERIALIZE;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        LogDebug( ( "Receiving RTCP type: %d", rtcpPacket.header.packetType ) );
        switch( rtcpPacket.header.packetType )
        {
            case RTCP_PACKET_FIR:
                // CHK_STATUS(onRtcpFIRPacket(&rtcpPacket, pKvsPeerConnection));
                break;
            case RTCP_PACKET_TRANSPORT_FEEDBACK_NACK:
                ret = OnRtcpNackEvent( pSession, &rtcpPacket );
                break;
            case RTCP_PACKET_TRANSPORT_FEEDBACK_TWCC:
                // if (rtcpPacket.header.receptionReportCount == RTCP_FEEDBACK_MESSAGE_TYPE_NACK) {
                //     CHK_STATUS(resendPacketOnNack(&rtcpPacket, pKvsPeerConnection));
                // } else if (rtcpPacket.header.receptionReportCount == RTCP_FEEDBACK_MESSAGE_TYPE_APPLICATION_LAYER_FEEDBACK) {
                //     CHK_STATUS(onRtcpTwccPacket(&rtcpPacket, pKvsPeerConnection));
                // } else {
                //     DLOGW("unhandled RTCP_PACKET_TYPE_GENERIC_RTP_FEEDBACK %d", rtcpPacket.header.receptionReportCount);
                // }
                break;
            case RTCP_PACKET_PAYLOAD_FEEDBACK_PLI:
            case RTCP_PACKET_PAYLOAD_FEEDBACK_SLI:
            case RTCP_PACKET_PAYLOAD_FEEDBACK_REMB:
                // if (rtcpPacket.header.receptionReportCount == RTCP_FEEDBACK_MESSAGE_TYPE_APPLICATION_LAYER_FEEDBACK &&
                //     isRembPacket(rtcpPacket.payload, rtcpPacket.payloadLength) == DTLS_SUCCESS) {
                //     CHK_STATUS(onRtcpRembPacket(&rtcpPacket, pKvsPeerConnection));
                // } else if (rtcpPacket.header.receptionReportCount == RTCP_PSFB_PLI) {
                //     CHK_STATUS(onRtcpPLIPacket(&rtcpPacket, pKvsPeerConnection));
                // } else if (rtcpPacket.header.receptionReportCount == RTCP_PSFB_SLI) {
                //     CHK_STATUS(onRtcpSLIPacket(&rtcpPacket, pKvsPeerConnection));
                // } else {
                //     DLOGW("unhandled packet type RTCP_PACKET_TYPE_PAYLOAD_SPECIFIC_FEEDBACK %d", rtcpPacket.header.receptionReportCount);
                // }
                break;
            case RTCP_PACKET_SENDER_REPORT:
                // CHK_STATUS(onRtcpSenderReport(&rtcpPacket, pKvsPeerConnection));
                break;
            case RTCP_PACKET_RECEIVER_REPORT:
                // CHK_STATUS(onRtcpReceiverReport(&rtcpPacket, pKvsPeerConnection));
                break;
            default:
                LogWarn( ( "unhandled packet type %d", rtcpPacket.header.packetType ) );
                break;
        }
    }

    return ret;
}
