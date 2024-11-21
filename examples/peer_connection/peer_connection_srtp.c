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
#include "h264_packetizer.h"
#include "opus_packetizer.h"
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
        LogDebug( ( "Fill frame with result: %d, length: %u, start seq: %u, end seq: %u",
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

static PeerConnectionResult_t ConstructSrtpPacket( PeerConnectionSession_t * pSession,
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
                bufferAfterEncrypt = 0;
                payloadType = pSession->rtpConfig.videoCodecRtxPayload;
                pRtpSeq = &pSession->rtpConfig.videoRtxSequenceNumber;
            }
        }
        else
        {
            pSrtpSender = &pSession->audioSrtpSender;
            payloadType = pSession->rtpConfig.audioCodecPayload;
            if( ( pSession->rtpConfig.audioCodecRtxPayload != 0 ) &&
                ( pSession->rtpConfig.audioCodecRtxPayload != pSession->rtpConfig.audioCodecPayload ) )
            {
                bufferAfterEncrypt = 0;
                payloadType = pSession->rtpConfig.audioCodecRtxPayload;
                pRtpSeq = &pSession->rtpConfig.audioRtxSequenceNumber;
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
            LogDebug( ( "Found target buffer, pRollingBufferPacket: %p, packetBufferLength: %u, pPacketBuffer: %p",
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
            ret = ConstructSrtpPacket( pSession,
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
            LogWarn( ( "Fail to re-send RTP packet, ret: %d, seq: %u, SSRC: 0x%lx", resultIceController, rtpSeq, ssrc ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_RESEND_RTP_PACKET;
        }
        else
        {
            LogDebug( ( "Re-send RTP successfully, RTP seq: %u, SSRC: 0x%lx", rtpSeq, ssrc ) );
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

PeerConnectionResult_t PeerConnectionSrtp_WriteH264Frame( PeerConnectionSession_t * pSession,
                                                          Transceiver_t * pTransceiver,
                                                          const PeerConnectionFrame_t * pFrame )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    H264PacketizerContext_t h264PacketizerContext;
    H264Result_t resultH264;
    H264Packet_t packetH264;
    uint8_t rtpBuffer[ PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH ];
    PeerConnectionRollingBufferPacket_t * pRollingBufferPacket = NULL;
    uint8_t * pSrtpPacket = NULL;
    size_t srtpPacketLength = 0;
    Nalu_t nalusArray[ PEER_CONNECTION_SRTP_H264_MAX_NALUS_IN_A_FRAME ];
    Frame_t h264Frame;
    PeerConnectionSrtpSender_t * pSrtpSender = NULL;
    uint8_t isLocked = 0;
    uint8_t bufferAfterEncrypt = 1;
    IceControllerResult_t resultIceController;
    uint16_t * pRtpSeq = NULL;
    uint32_t payloadType;
    uint32_t * pSsrc = NULL;
    /* For TWCC ID extension info. */
    uint32_t extensionPayload;

    if( ( pSession == NULL ) ||
        ( pTransceiver == NULL ) ||
        ( pFrame == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pTransceiver: %p, pFrame: %p",
                    pSession, pTransceiver, pFrame ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultH264 = H264Packetizer_Init( &h264PacketizerContext,
                                          nalusArray,
                                          PEER_CONNECTION_SRTP_H264_MAX_NALUS_IN_A_FRAME );
        if( resultH264 != H264_RESULT_OK )
        {
            LogError( ( "Fail to init H264 packetizer, result: %d", resultH264 ) );
            ret = PEER_CONNECTION_RESULT_FAIL_PACKETIZER_INIT;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        h264Frame.pFrameData = pFrame->pData;
        h264Frame.frameDataLength = pFrame->dataLength;
        resultH264 = H264Packetizer_AddFrame( &h264PacketizerContext,
                                              &h264Frame );
        if( resultH264 != H264_RESULT_OK )
        {
            LogError( ( "Fail to init H264 packetizer, result: %d", resultH264 ) );
            ret = PEER_CONNECTION_RESULT_FAIL_PACKETIZER_ADD_FRAME;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSsrc = &pTransceiver->ssrc;
        if( pTransceiver->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO )
        {
            pSrtpSender = &pSession->videoSrtpSender;
            pRtpSeq = &pSession->rtpConfig.videoSequenceNumber;
            payloadType = pSession->rtpConfig.videoCodecPayload;
            if( ( pSession->rtpConfig.videoCodecRtxPayload != 0 ) &&
                ( pSession->rtpConfig.videoCodecRtxPayload != pSession->rtpConfig.videoCodecPayload ) )
            {
                bufferAfterEncrypt = 0;
            }
        }
        else
        {
            pSrtpSender = &pSession->audioSrtpSender;
            pRtpSeq = &pSession->rtpConfig.audioSequenceNumber;
            payloadType = pSession->rtpConfig.audioCodecPayload;
            if( ( pSession->rtpConfig.audioCodecRtxPayload != 0 ) &&
                ( pSession->rtpConfig.audioCodecRtxPayload != pSession->rtpConfig.audioCodecPayload ) )
            {
                bufferAfterEncrypt = 0;
            }
        }

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

    while( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Get buffer from sender for later use.
         * PeerConnectionRollingBuffer_GetRtpSequenceBuffer() returns the buffer with its size.
         * If the bufferAfterEncrypt = 0, we store only RTP payload to the buffer.
         * If the bufferAfterEncrypt = 1, we store the encrypted SRTP packet to the buffer. */
        pRollingBufferPacket = NULL;
        ret = PeerConnectionRollingBuffer_GetRtpSequenceBuffer( &pSrtpSender->txRollingBuffer,
                                                                *pRtpSeq,
                                                                &pRollingBufferPacket );
        if( ret != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "Fail to get RTP buffer for seq: %u", *pRtpSeq ) );
            break;
        }

        /* Get each NALU payload then serialize SRTP packet. */
        if( bufferAfterEncrypt == 0 )
        {
            packetH264.pPacketData = pRollingBufferPacket->pPacketBuffer + PEER_CONNECTION_SRTP_RTX_WRITE_RESERVED_BYTES;
            packetH264.packetDataLength = pRollingBufferPacket->packetBufferLength - PEER_CONNECTION_SRTP_RTX_WRITE_RESERVED_BYTES;

            /* Using local buffer for SRTP packet, use the entire packet length. */
            pSrtpPacket = rtpBuffer;
            srtpPacketLength = PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH;
        }
        else
        {
            /* Using local buffer for RTP payload only, set RTP payload length. */
            packetH264.pPacketData = rtpBuffer;
            packetH264.packetDataLength = PEER_CONNECTION_SRTP_RTP_PAYLOAD_MAX_LENGTH;

            pSrtpPacket = pRollingBufferPacket->pPacketBuffer;
            srtpPacketLength = pRollingBufferPacket->packetBufferLength;
        }

        resultH264 = H264Packetizer_GetPacket( &h264PacketizerContext,
                                               &packetH264 );
        if( resultH264 == H264_RESULT_NO_MORE_PACKETS )
        {
            PeerConnectionRollingBuffer_DiscardRtpSequenceBuffer( &pSrtpSender->txRollingBuffer,
                                                                  pRollingBufferPacket );
            /* Eraly break because no packet available. */
            break;
        }
        else if( resultH264 == H264_RESULT_OK )
        {
            /* Prepare RTP packet for each payload buffer. */
            memset( &pRollingBufferPacket->rtpPacket, 0, sizeof( RtpPacket_t ) );
            pRollingBufferPacket->rtpPacket.header.payloadType = payloadType;
            pRollingBufferPacket->rtpPacket.header.sequenceNumber = *pRtpSeq;
            pRollingBufferPacket->rtpPacket.header.ssrc = *pSsrc;
            if( h264PacketizerContext.naluCount == 0 )
            {
                /* This is the last packet, set the marker. */
                pRollingBufferPacket->rtpPacket.header.flags |= RTP_HEADER_FLAG_MARKER;
            }

            pRollingBufferPacket->rtpPacket.header.csrcCount = 0;
            pRollingBufferPacket->rtpPacket.header.pCsrc = NULL;
            pRollingBufferPacket->rtpPacket.header.timestamp = PEER_CONNECTION_SRTP_CONVERT_TIME_US_TO_RTP_TIMESTAMP( PEER_CONNECTION_SRTP_VIDEO_CLOCKRATE, pFrame->presentationUs );

            if( pSession->rtpConfig.twccId > 0 )
            {
                pRollingBufferPacket->rtpPacket.header.flags |= RTP_HEADER_FLAG_EXTENSION;
                pRollingBufferPacket->rtpPacket.header.extension.extensionProfile = PEER_CONNECTION_SRTP_TWCC_EXT_PROFILE;
                pRollingBufferPacket->rtpPacket.header.extension.extensionPayloadLength = 1;
                extensionPayload = PEER_CONNECTION_SRTP_GET_TWCC_PAYLOAD( pSession->rtpConfig.twccId, pSession->rtpConfig.twccSequence );
                pRollingBufferPacket->rtpPacket.header.extension.pExtensionPayload = &extensionPayload;
                pSession->rtpConfig.twccSequence++;
            }

            pRollingBufferPacket->rtpPacket.payloadLength = packetH264.packetDataLength;
            pRollingBufferPacket->rtpPacket.pPayload = packetH264.pPacketData;

            /* ConstructSrtpPacket() serializes RTP packet and encrypt it. */
            ret = ConstructSrtpPacket( pSession,
                                       &pRollingBufferPacket->rtpPacket,
                                       pSrtpPacket,
                                       &srtpPacketLength );
        }
        else
        {
            LogError( ( "Fail to get H264 packet, result: %d", resultH264 ) );
            ret = PEER_CONNECTION_RESULT_FAIL_PACKETIZER_GET_PACKET;
        }

        if( ret == PEER_CONNECTION_RESULT_OK )
        {
            /* Update the rolling buffer length before storing. */
            if( bufferAfterEncrypt == 0 )
            {
                pRollingBufferPacket->packetBufferLength = packetH264.packetDataLength;
            }
            else
            {
                pRollingBufferPacket->packetBufferLength = srtpPacketLength;
            }

            /* Udpate the packet into rolling buffer. */
            ret = PeerConnectionRollingBuffer_SetPacket( &pSrtpSender->txRollingBuffer,
                                                         ( *pRtpSeq )++,
                                                         pRollingBufferPacket );
        }

        if( ( ret != PEER_CONNECTION_RESULT_OK ) && ( pRollingBufferPacket != NULL ) )
        {
            /* If any failure, release the allocated RTP buffer. */
            PeerConnectionRollingBuffer_DiscardRtpSequenceBuffer( &pSrtpSender->txRollingBuffer,
                                                                  pRollingBufferPacket );
        }

        /* Write the constructed RTP packets through network. */
        if( ret == PEER_CONNECTION_RESULT_OK )
        {
            resultIceController = IceController_SendToRemotePeer( &pSession->iceControllerContext,
                                                                  pSrtpPacket,
                                                                  srtpPacketLength );
            if( resultIceController != ICE_CONTROLLER_RESULT_OK )
            {
                LogWarn( ( "Fail to send RTP packet, ret: %d", resultIceController ) );
                ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_SEND_RTP_PACKET;
            }
        }

        if( ret == PEER_CONNECTION_RESULT_OK )
        {
            Metric_EndEvent( METRIC_EVENT_SENDING_FIRST_FRAME );
        }
    }

    if( isLocked )
    {
        pthread_mutex_unlock( &( pSrtpSender->senderMutex ) );
    }

    return ret;
}

PeerConnectionResult_t PeerConnectionSrtp_WriteOpusFrame( PeerConnectionSession_t * pSession,
                                                          Transceiver_t * pTransceiver,
                                                          const PeerConnectionFrame_t * pFrame )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    OpusPacketizerContext_t opusPacketizerContext;
    OpusResult_t resultOpus;
    OpusPacket_t packetOpus;
    OpusFrame_t opusFrame;
    uint8_t rtpBuffer[ PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH ];
    PeerConnectionRollingBufferPacket_t * pRollingBufferPacket = NULL;
    uint8_t * pSrtpPacket = NULL;
    size_t srtpPacketLength = 0;
    PeerConnectionSrtpSender_t * pSrtpSender = NULL;
    uint8_t isLocked = 0;
    uint8_t bufferAfterEncrypt = 1;
    IceControllerResult_t resultIceController;
    uint16_t * pRtpSeq = NULL;
    uint32_t payloadType;
    uint32_t * pSsrc = NULL;
    /* For TWCC ID extension info. */
    uint32_t extensionPayload;

    if( ( pSession == NULL ) ||
        ( pTransceiver == NULL ) ||
        ( pFrame == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pTransceiver: %p, pFrame: %p",
                    pSession, pTransceiver, pFrame ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        opusFrame.pFrameData = pFrame->pData;
        opusFrame.frameDataLength = pFrame->dataLength;

        resultOpus = OpusPacketizer_Init( &opusPacketizerContext,
                                          &opusFrame );
        if( resultOpus != OPUS_RESULT_OK )
        {
            LogError( ( "Fail to init OPUS packetizer, result: %d", resultOpus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_PACKETIZER_INIT;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSsrc = &pTransceiver->ssrc;
        if( pTransceiver->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO )
        {
            pSrtpSender = &pSession->videoSrtpSender;
            pRtpSeq = &pSession->rtpConfig.videoSequenceNumber;
            payloadType = pSession->rtpConfig.videoCodecPayload;
            if( ( pSession->rtpConfig.videoCodecRtxPayload != 0 ) &&
                ( pSession->rtpConfig.videoCodecRtxPayload != pSession->rtpConfig.videoCodecPayload ) )
            {
                bufferAfterEncrypt = 0;
            }
        }
        else
        {
            pSrtpSender = &pSession->audioSrtpSender;
            pRtpSeq = &pSession->rtpConfig.audioSequenceNumber;
            payloadType = pSession->rtpConfig.audioCodecPayload;
            if( ( pSession->rtpConfig.audioCodecRtxPayload != 0 ) &&
                ( pSession->rtpConfig.audioCodecRtxPayload != pSession->rtpConfig.audioCodecPayload ) )
            {
                bufferAfterEncrypt = 0;
            }
        }

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

    while( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Get buffer from sender for later use.
         * PeerConnectionRollingBuffer_GetRtpSequenceBuffer() returns the buffer with its size.
         * If the bufferAfterEncrypt = 0, we store only RTP payload to the buffer.
         * If the bufferAfterEncrypt = 1, we store the encrypted SRTP packet to the buffer. */
        pRollingBufferPacket = NULL;
        ret = PeerConnectionRollingBuffer_GetRtpSequenceBuffer( &pSrtpSender->txRollingBuffer,
                                                                *pRtpSeq,
                                                                &pRollingBufferPacket );
        if( ret != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "Fail to get RTP buffer for seq: %u", *pRtpSeq ) );
            break;
        }

        /* Get each NALU payload then serialize SRTP packet. */
        if( bufferAfterEncrypt == 0 )
        {
            packetOpus.pPacketData = pRollingBufferPacket->pPacketBuffer + PEER_CONNECTION_SRTP_RTX_WRITE_RESERVED_BYTES;
            packetOpus.packetDataLength = pRollingBufferPacket->packetBufferLength - PEER_CONNECTION_SRTP_RTX_WRITE_RESERVED_BYTES;

            /* Using local buffer for SRTP packet, use the entire packet length. */
            pSrtpPacket = rtpBuffer;
            srtpPacketLength = PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH;
        }
        else
        {
            /* Using local buffer for RTP payload only, set RTP payload length. */
            packetOpus.pPacketData = rtpBuffer;
            packetOpus.packetDataLength = PEER_CONNECTION_SRTP_RTP_PAYLOAD_MAX_LENGTH;

            pSrtpPacket = pRollingBufferPacket->pPacketBuffer;
            srtpPacketLength = pRollingBufferPacket->packetBufferLength;
        }

        resultOpus = OpusPacketizer_GetPacket( &opusPacketizerContext,
                                               &packetOpus );
        if( resultOpus == OPUS_RESULT_NO_MORE_PACKETS )
        {
            PeerConnectionRollingBuffer_DiscardRtpSequenceBuffer( &pSrtpSender->txRollingBuffer,
                                                                  pRollingBufferPacket );
            /* Eraly break because no packet available. */
            break;
        }
        else if( resultOpus == OPUS_RESULT_OK )
        {
            /* Prepare RTP packet for each payload buffer. */
            memset( &pRollingBufferPacket->rtpPacket, 0, sizeof( RtpPacket_t ) );
            pRollingBufferPacket->rtpPacket.header.payloadType = payloadType;
            pRollingBufferPacket->rtpPacket.header.sequenceNumber = *pRtpSeq;
            pRollingBufferPacket->rtpPacket.header.ssrc = *pSsrc;

            pRollingBufferPacket->rtpPacket.header.csrcCount = 0;
            pRollingBufferPacket->rtpPacket.header.pCsrc = NULL;
            pRollingBufferPacket->rtpPacket.header.timestamp = PEER_CONNECTION_SRTP_CONVERT_TIME_US_TO_RTP_TIMESTAMP( PEER_CONNECTION_SRTP_VIDEO_CLOCKRATE, pFrame->presentationUs );

            if( pSession->rtpConfig.twccId > 0 )
            {
                pRollingBufferPacket->rtpPacket.header.flags |= RTP_HEADER_FLAG_EXTENSION;
                pRollingBufferPacket->rtpPacket.header.extension.extensionProfile = PEER_CONNECTION_SRTP_TWCC_EXT_PROFILE;
                pRollingBufferPacket->rtpPacket.header.extension.extensionPayloadLength = 1;
                extensionPayload = PEER_CONNECTION_SRTP_GET_TWCC_PAYLOAD( pSession->rtpConfig.twccId, pSession->rtpConfig.twccSequence );
                pRollingBufferPacket->rtpPacket.header.extension.pExtensionPayload = &extensionPayload;
                pSession->rtpConfig.twccSequence++;
            }

            pRollingBufferPacket->rtpPacket.payloadLength = packetOpus.packetDataLength;
            pRollingBufferPacket->rtpPacket.pPayload = packetOpus.pPacketData;

            /* ConstructSrtpPacket() serializes RTP packet and encrypt it. */
            ret = ConstructSrtpPacket( pSession,
                                       &pRollingBufferPacket->rtpPacket,
                                       pSrtpPacket,
                                       &srtpPacketLength );
        }
        else
        {
            LogError( ( "Fail to get OPUS packet, result: %d", resultOpus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_PACKETIZER_GET_PACKET;
        }

        if( ret == PEER_CONNECTION_RESULT_OK )
        {
            /* Update the rolling buffer length before storing. */
            if( bufferAfterEncrypt == 0 )
            {
                pRollingBufferPacket->packetBufferLength = packetOpus.packetDataLength;
            }
            else
            {
                pRollingBufferPacket->packetBufferLength = srtpPacketLength;
            }

            /* Udpate the packet into rolling buffer. */
            ret = PeerConnectionRollingBuffer_SetPacket( &pSrtpSender->txRollingBuffer,
                                                         ( *pRtpSeq )++,
                                                         pRollingBufferPacket );
        }

        if( ( ret != PEER_CONNECTION_RESULT_OK ) && ( pRollingBufferPacket != NULL ) )
        {
            /* If any failure, release the allocated RTP buffer. */
            PeerConnectionRollingBuffer_DiscardRtpSequenceBuffer( &pSrtpSender->txRollingBuffer,
                                                                  pRollingBufferPacket );
        }

        /* Write the constructed RTP packets through network. */
        if( ret == PEER_CONNECTION_RESULT_OK )
        {
            resultIceController = IceController_SendToRemotePeer( &pSession->iceControllerContext,
                                                                  pSrtpPacket,
                                                                  srtpPacketLength );
            if( resultIceController != ICE_CONTROLLER_RESULT_OK )
            {
                LogWarn( ( "Fail to send RTP packet, ret: %d", resultIceController ) );
                ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_SEND_RTP_PACKET;
            }
        }

        if( ret == PEER_CONNECTION_RESULT_OK )
        {
            Metric_EndEvent( METRIC_EVENT_SENDING_FIRST_FRAME );
        }
    }

    if( isLocked )
    {
        pthread_mutex_unlock( &( pSrtpSender->senderMutex ) );
    }

    return ret;
}

PeerConnectionResult_t PeerConnectionSrtp_HandleSrtpPacket( PeerConnectionSession_t * pSession,
                                                            uint8_t * pBuffer,
                                                            size_t bufferLength )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    srtp_err_status_t errorStatus;
    static uint8_t rtpBuffer[ PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH ];
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
            LogVerbose( ( "Decrypt SRTP packet successfully, decrypted length: %u", rtpBufferLength ) );
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
            LogWarn( ( "Received unknown SSRC: %lu RTP packet.", rtpPacket.header.ssrc ) );
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
            LogVerbose( ( "Decrypt SRTCP packet successfully, decrypted length: %u", rtcpBufferLength ) );
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
                //     isRembPacket(rtcpPacket.payload, rtcpPacket.payloadLength) == STATUS_SUCCESS) {
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
