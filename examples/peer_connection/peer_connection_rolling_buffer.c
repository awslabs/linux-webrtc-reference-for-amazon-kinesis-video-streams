#include <stdlib.h>
#include "logging.h"
#include "peer_connection.h"
#include "peer_connection_rolling_buffer.h"

//#include "FreeRTOS.h"

PeerConnectionResult_t PeerConnectionRollingBuffer_Create( PeerConnectionRollingBuffer_t * pRollingBuffer,
                                                           uint32_t rollingbufferBitRate,  // bps
                                                           uint32_t rollingbufferDurationSec,  // duration in seconds
                                                           size_t maxSizePerPacket )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    RtpPacketInfo_t * pRtpPacketInfoArray;
    RtpPacketQueueResult_t resultRtpPacketQueue;

    if( ( pRollingBuffer == NULL ) ||
        ( rollingbufferBitRate == 0 ) ||
        ( rollingbufferDurationSec == 0 ) ||
        ( maxSizePerPacket == 0 ) )
    {
        LogError( ( "Invalid input, pRollingBuffer: %p, rollingbufferBitRate: %u, pFrame: %u, maxSizePerPacket: %lu",
                    pRollingBuffer,
                    rollingbufferBitRate,
                    rollingbufferDurationSec,
                    maxSizePerPacket ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pRollingBuffer->maxSizePerPacket = maxSizePerPacket;
        pRollingBuffer->capacity = rollingbufferDurationSec * rollingbufferBitRate / 8U / maxSizePerPacket;
        pRtpPacketInfoArray = ( RtpPacketInfo_t * )malloc( pRollingBuffer->capacity * sizeof( RtpPacketInfo_t ) );
        if( pRtpPacketInfoArray == NULL )
        {
            LogError( ( "No memory available for allocating RTP packet info array with total size %lu, capacity: %lu, sizeof( RtpPacketInfo_t ): %lu",
                        pRollingBuffer->capacity * sizeof( RtpPacketInfo_t ),
                        pRollingBuffer->capacity,
                        sizeof( RtpPacketInfo_t ) ) );
            ret = PEER_CONNECTION_RESULT_FAIL_PACKET_INFO_NO_ENOUGH_MEMORY;
        }
        else
        {
            LogInfo( ( "Allocated RTP packet info array with total size %lu, capacity: %lu, sizeof( RtpPacketInfo_t ): %lu",
                       pRollingBuffer->capacity * sizeof( RtpPacketInfo_t ),
                       pRollingBuffer->capacity,
                       sizeof( RtpPacketInfo_t ) ) );
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultRtpPacketQueue = RtpPacketQueue_Init( &pRollingBuffer->packetQueue,
                                                    pRtpPacketInfoArray,
                                                    pRollingBuffer->capacity );
        if( resultRtpPacketQueue != RTP_PACKET_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to init RTP packet queue with result: %d", resultRtpPacketQueue ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTP_PACKET_QUEUE_INIT;
        }
    }

    return ret;
}

void PeerConnectionRollingBuffer_Free( PeerConnectionRollingBuffer_t * pRollingBuffer )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    RtpPacketQueueResult_t resultRtpPacketQueue = RTP_PACKET_QUEUE_RESULT_OK;
    RtpPacketInfo_t rtpPacketInfo;

    if( pRollingBuffer == NULL )
    {
        LogError( ( "Invalid input, pRollingBuffer: %p",
                    pRollingBuffer ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        while( resultRtpPacketQueue == RTP_PACKET_QUEUE_RESULT_OK )
        {
            resultRtpPacketQueue = RtpPacketQueue_Dequeue( &pRollingBuffer->packetQueue,
                                                           &rtpPacketInfo );
            if( ( resultRtpPacketQueue == RTP_PACKET_QUEUE_RESULT_OK ) && ( rtpPacketInfo.pSerializedRtpPacket != NULL ) )
            {
                free( rtpPacketInfo.pSerializedRtpPacket );
            }
        }

        if( pRollingBuffer->packetQueue.pRtpPacketInfoArray != NULL )
        {
            free( pRollingBuffer->packetQueue.pRtpPacketInfoArray );
        }
    }
}

PeerConnectionResult_t PeerConnectionRollingBuffer_GetRtpSequenceBuffer( PeerConnectionRollingBuffer_t * pRollingBuffer,
                                                                         uint16_t rtpSeq,
                                                                         PeerConnectionRollingBufferPacket_t ** ppPacket )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( ( pRollingBuffer == NULL ) ||
        ( ppPacket == NULL ) )
    {
        LogError( ( "Invalid input, pRollingBuffer: %p, ppPacket: %p",
                    pRollingBuffer, ppPacket ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else if( pRollingBuffer->capacity == 0 )
    {
        LogError( ( "Rolling buffer is not initialized yet." ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {
        *ppPacket = ( PeerConnectionRollingBufferPacket_t * )malloc( sizeof( PeerConnectionRollingBufferPacket_t ) + pRollingBuffer->maxSizePerPacket );
        ( *ppPacket )->pPacketBuffer = ( uint8_t * )( ( *ppPacket ) + 1 );
        ( *ppPacket )->packetBufferLength = pRollingBuffer->maxSizePerPacket;
    }

    return ret;
}

void PeerConnectionRollingBuffer_DiscardRtpSequenceBuffer( PeerConnectionRollingBuffer_t * pRollingBuffer,
                                                           PeerConnectionRollingBufferPacket_t * pPacket )
{
    ( void ) pRollingBuffer;
    if( pPacket )
    {
        free( pPacket );
    }
}

PeerConnectionResult_t PeerConnectionRollingBuffer_SearchRtpSequenceBuffer( PeerConnectionRollingBuffer_t * pRollingBuffer,
                                                                            uint16_t rtpSeq,
                                                                            PeerConnectionRollingBufferPacket_t ** ppPacket )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    RtpPacketQueueResult_t resultRtpPacketQueue = RTP_PACKET_QUEUE_RESULT_OK;
    RtpPacketInfo_t rtpPacketInfo;

    if( ( pRollingBuffer == NULL ) ||
        ( ppPacket == NULL ) )
    {
        LogError( ( "Invalid input, pRollingBuffer: %p, ppPacket: %p",
                    pRollingBuffer, ppPacket ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else if( pRollingBuffer->capacity == 0 )
    {
        LogError( ( "Rolling buffer is not initialized yet." ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {
        /* Empty else marker. */
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultRtpPacketQueue = RtpPacketQueue_Retrieve( &pRollingBuffer->packetQueue,
                                                        rtpSeq,
                                                        &rtpPacketInfo );
        if( resultRtpPacketQueue != RTP_PACKET_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to retrieve RTP packet sequence number: %u with result: %d",
                        rtpSeq,
                        resultRtpPacketQueue ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTP_PACKET_QUEUE_RETRIEVE;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        *ppPacket = ( PeerConnectionRollingBufferPacket_t * )rtpPacketInfo.pSerializedRtpPacket;
        ( *ppPacket )->packetBufferLength = rtpPacketInfo.serializedPacketLength;
    }

    return ret;
}

PeerConnectionResult_t PeerConnectionRollingBuffer_SetPacket( PeerConnectionRollingBuffer_t * pRollingBuffer,
                                                              uint16_t rtpSeq,
                                                              PeerConnectionRollingBufferPacket_t * pPacket )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    RtpPacketQueueResult_t resultRtpPacketQueue = RTP_PACKET_QUEUE_RESULT_OK;
    RtpPacketInfo_t rtpPacket, deletedRtpPacket;

    if( ( pRollingBuffer == NULL ) || ( pPacket == NULL ) )
    {
        LogError( ( "Invalid input, pRollingBuffer: %p, pPacket: %p",
                    pRollingBuffer, pPacket ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else if( pRollingBuffer->capacity == 0 )
    {
        LogError( ( "Rolling buffer is not initialized yet." ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {
        /* Empty else marker. */
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        memset( &rtpPacket, 0, sizeof( RtpPacketInfo_t ) );
        memset( &deletedRtpPacket, 0, sizeof( RtpPacketInfo_t ) );
        rtpPacket.pSerializedRtpPacket = ( uint8_t * )pPacket;
        rtpPacket.seqNum = rtpSeq;
        rtpPacket.serializedPacketLength = pPacket->packetBufferLength;
        resultRtpPacketQueue = RtpPacketQueue_ForceEnqueue( &pRollingBuffer->packetQueue,
                                                            &rtpPacket,
                                                            &deletedRtpPacket );
        if( ( resultRtpPacketQueue != RTP_PACKET_QUEUE_RESULT_OK ) && ( resultRtpPacketQueue != RTP_PACKET_QUEUE_RESULT_PACKET_DELETED ) )
        {
            LogError( ( "Fail to enqueue RTP packet sequence number: %u with result: %d",
                        rtpSeq,
                        resultRtpPacketQueue ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTP_PACKET_ENQUEUE;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        if( deletedRtpPacket.pSerializedRtpPacket )
        {
            PeerConnectionRollingBuffer_DiscardRtpSequenceBuffer( pRollingBuffer, ( PeerConnectionRollingBufferPacket_t * ) deletedRtpPacket.pSerializedRtpPacket );
        }
    }

    return ret;
}
