#include "peer_connection_codec_helper.h"
#include "opus_packetizer.h"
#include "opus_depacketizer.h"

PeerConnectionResult_t GetOpusPacketProperty( PeerConnectionJitterBufferPacket_t * pPacket,
                                              uint8_t * pIsStartPacket )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    OpusResult_t resultOpus;
    uint32_t properties = 0;

    if( ( pPacket == NULL ) ||
        ( pIsStartPacket == NULL ) )
    {
        LogError( ( "Invalid input, pPacket: %p, pIsStartPacket: %p", pPacket, pIsStartPacket ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultOpus = OpusDepacketizer_GetPacketProperties( pPacket->pPacketBuffer,
                                                           pPacket->packetBufferLength,
                                                           &properties );

        if( resultOpus != OPUS_RESULT_OK )
        {
            LogError( ( "Fail to get OPUS packet properties, result: %d", resultOpus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_DEPACKETIZER_GET_PROPERTIES;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        *pIsStartPacket = 0U;
        if( ( properties & OPUS_PACKET_PROPERTY_START_PACKET ) != 0 )
        {
            *pIsStartPacket = 1U;
        }
    }

    return ret;
}

PeerConnectionResult_t FillFrameOpus( PeerConnectionJitterBuffer_t * pJitterBuffer,
                                      uint16_t rtpSeqStart,
                                      uint16_t rtpSeqEnd,
                                      uint8_t * pOutBuffer,
                                      size_t * pOutBufferLength,
                                      uint32_t * pRtpTimestamp )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    uint16_t i, index;
    PeerConnectionJitterBufferPacket_t * pPacket;
    OpusResult_t resultOpus;
    OpusDepacketizerContext_t opusDepacketizerContext;
    OpusPacket_t opusPackets[ PEER_CONNECTION_JITTER_BUFFER_MAX_PACKETS_NUM_IN_A_FRAME ];
    OpusPacket_t opusPacket;
    OpusFrame_t frame;
    uint32_t rtpTimestamp = 0;

    if( ( pJitterBuffer == NULL ) ||
        ( pOutBuffer == NULL ) ||
        ( pOutBufferLength == NULL ) ||
        ( pRtpTimestamp == NULL ) )
    {
        LogError( ( "Invalid input, pJitterBuffer: %p, pOutBuffer: %p, pOutBufferLength: %p, pRtpTimestamp: %p", pJitterBuffer, pOutBuffer, pOutBufferLength, pRtpTimestamp ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultOpus = OpusDepacketizer_Init( &opusDepacketizerContext,
                                            opusPackets,
                                            PEER_CONNECTION_JITTER_BUFFER_MAX_PACKETS_NUM_IN_A_FRAME );
        if( resultOpus != OPUS_RESULT_OK )
        {
            LogError( ( "Fail to initialize OPUS depacketizer, result: %d", resultOpus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_DEPACKETIZER_INIT;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        for( i = rtpSeqStart; i != rtpSeqEnd + 1; i++ )
        {
            index = PEER_CONNECTION_JITTER_BUFFER_WRAP( i, PEER_CONNECTION_JITTER_BUFFER_MAX_ENTRY_NUM );
            pPacket = &pJitterBuffer->rtpPackets[ index ];
            opusPacket.pPacketData = pPacket->pPacketBuffer;
            opusPacket.packetDataLength = pPacket->packetBufferLength;
            rtpTimestamp = pPacket->rtpTimestamp;
            LogDebug( ( "Adding packet seq: %u, length: %lu, timestamp: %u", i, opusPacket.packetDataLength, rtpTimestamp ) );

            resultOpus = OpusDepacketizer_AddPacket( &opusDepacketizerContext,
                                                     &opusPacket );
            if( resultOpus != OPUS_RESULT_OK )
            {
                LogError( ( "Fail to add OPUS depacketizer packet, result: %d", resultOpus ) );
                ret = PEER_CONNECTION_RESULT_FAIL_DEPACKETIZER_ADD_PACKET;
                break;
            }
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        frame.pFrameData = pOutBuffer;
        frame.frameDataLength = *pOutBufferLength;
        resultOpus = OpusDepacketizer_GetFrame( &opusDepacketizerContext,
                                                &frame );
        if( resultOpus != OPUS_RESULT_OK )
        {
            LogError( ( "Fail to get OPUS depacketizer frame, result: %d", resultOpus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_DEPACKETIZER_GET_FRAME;
        }

    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        *pOutBufferLength = frame.frameDataLength;
        *pRtpTimestamp = rtpTimestamp;
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
    uint8_t rtpBuffer[ PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH ];
    PeerConnectionRollingBufferPacket_t * pRollingBufferPacket = NULL;
    uint8_t * pSrtpPacket = NULL;
    size_t srtpPacketLength = 0;
    OpusFrame_t opusFrame;
    PeerConnectionSrtpSender_t * pSrtpSender = NULL;
    uint8_t isLocked = 0;
    uint8_t bufferAfterEncrypt = 1;
    IceControllerResult_t resultIceController;
    uint16_t * pRtpSeq = NULL;
    uint32_t payloadType;
    uint32_t * pSsrc = NULL;
    uint32_t packetSent = 0;
    uint32_t bytesSent = 0;
    uint32_t randomRtpTimeoffset = 0;    // TODO : Spec required random rtp time offset ( current implementation of KVS SDK )

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

    if( pTransceiver->trackKind != TRANSCEIVER_TRACK_KIND_AUDIO )
    {
        LogError( ( "Invalid track kind." ) );
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
            LogError( ( "Fail to init Opus packetizer, result: %d", resultOpus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_PACKETIZER_INIT;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSsrc = &pTransceiver->ssrc;
        pSrtpSender = &pSession->audioSrtpSender;
        pRtpSeq = &pSession->rtpConfig.audioSequenceNumber;
        payloadType = pSession->rtpConfig.audioCodecPayload;
        if( ( pSession->rtpConfig.audioCodecRtxPayload != 0 ) &&
            ( pSession->rtpConfig.audioCodecRtxPayload != pSession->rtpConfig.audioCodecPayload ) )
        {
            bufferAfterEncrypt = 0;
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

            /* For Opus, typically each packet is complete, so we set the marker bit for each packet */
            pRollingBufferPacket->rtpPacket.header.flags |= RTP_HEADER_FLAG_MARKER;

            pRollingBufferPacket->rtpPacket.header.csrcCount = 0;
            pRollingBufferPacket->rtpPacket.header.pCsrc = NULL;
            pRollingBufferPacket->rtpPacket.header.timestamp = PEER_CONNECTION_SRTP_CONVERT_TIME_US_TO_RTP_TIMESTAMP( PEER_CONNECTION_SRTP_OPUS_CLOCKRATE,
                                                                                                                      pFrame->presentationUs );

            if( pSession->rtpConfig.twccId > 0 )
            {
                pRollingBufferPacket->rtpPacket.header.flags |= RTP_HEADER_FLAG_EXTENSION;
                pRollingBufferPacket->rtpPacket.header.extension.extensionProfile = PEER_CONNECTION_SRTP_TWCC_EXT_PROFILE;
                pRollingBufferPacket->rtpPacket.header.extension.extensionPayloadLength = 1;
                extensionPayload = PEER_CONNECTION_SRTP_GET_TWCC_PAYLOAD( pSession->rtpConfig.twccId,
                                                                          pSession->rtpConfig.twccSequence );
                pRollingBufferPacket->rtpPacket.header.extension.pExtensionPayload = &extensionPayload;
                pSession->rtpConfig.twccSequence++;
            }

            pRollingBufferPacket->rtpPacket.payloadLength = packetOpus.packetDataLength;
            pRollingBufferPacket->rtpPacket.pPayload = packetOpus.pPacketData;

            /* PeerConnectionSrtp_ConstructSrtpPacket() serializes RTP packet and encrypt it. */
            ret = PeerConnectionSrtp_ConstructSrtpPacket( pSession,
                                                          &pRollingBufferPacket->rtpPacket,
                                                          pSrtpPacket,
                                                          &srtpPacketLength );
        }
        else
        {
            LogError( ( "Fail to get Opus packet, result: %d", resultOpus ) );
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
            packetSent++;
            bytesSent += pRollingBufferPacket->rtpPacket.payloadLength;
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

    if( pTransceiver->rtpSender.rtpFirstFrameWallClockTimeUs == 0 )
    {
        pTransceiver->rtpSender.rtpFirstFrameWallClockTimeUs = NetworkingUtils_GetCurrentTimeUs( NULL );
        pTransceiver->rtpSender.rtpTimeOffset = randomRtpTimeoffset;
    }

    pTransceiver->rtcpStats.rtpPacketsTransmitted += packetSent;
    pTransceiver->rtcpStats.rtpBytesTransmitted += bytesSent;

    return ret;
}
