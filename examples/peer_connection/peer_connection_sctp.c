#include <string.h>
#include <assert.h>

#include "ice_controller_data_types.h"
#include "peer_connection_sctp.h"
/*-----------------------------------------------------------*/

#if ENABLE_SCTP_DATA_CHANNEL

static PeerConnectionDataChannel_t globalDataChannels[MAX_SCTP_DATA_CHANNELS];
static int uKVSDataChannelCount = 0;
/*-----------------------------------------------------------*/

static void onSCTPSessionOutboundPacket( void * customData,
                                         uint8_t * pPacket,
                                         uint32_t packetLen );
static void onSCTPSessionDataChannelOpen( void * customData,
                                          uint32_t channelId,
                                          uint8_t * pName,
                                          uint32_t nameLen );
static void onSCTPSessionDataChannelMessage( void * customData,
                                             uint32_t channelId,
                                             uint8_t isBinary,
                                             uint8_t * pMessage,
                                             uint32_t pMessageLen );
/*-----------------------------------------------------------*/

/* Allocate a SCTP data channel from the global array of data channel */
PeerConnectionDataChannel_t * PeerConnectionSCTP_AllocateDataChannel( uint32_t * ulChannelID )
{

    PeerConnectionDataChannel_t * pChannel = NULL;

    if( uKVSDataChannelCount < MAX_SCTP_DATA_CHANNELS )
    {
        uint32_t ulIter = 0;
        for(; ulIter < MAX_SCTP_DATA_CHANNELS; ulIter++ )
        {
            if( globalDataChannels[ulIter].ucChannelActive == 0 )
            {
                pChannel = &globalDataChannels[ulIter];
                globalDataChannels[ulIter].ucChannelActive = 1;
                if( ulChannelID != NULL )
                {
                    *ulChannelID = ulIter;
                }
                uKVSDataChannelCount++;
                break;
            }

        }
    }

    return pChannel;

}
/*-----------------------------------------------------------*/

/* Free the data channel */
PeerConnectionResult_t PeerConnectionSCTP_DeallocateDataChannel( PeerConnectionDataChannel_t * pChannel )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    if( pChannel == NULL )
    {
        LogError( ( "Invalid input, pChannel: %p", pChannel ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {
        pChannel->ucChannelActive = 0;
        if( uKVSDataChannelCount > 0 )
        {
            uKVSDataChannelCount--;
        }
    }

    return ret;

}
/*-----------------------------------------------------------*/

/* Create and configure a data channel that will be established once
 * SCTP session is active. */
PeerConnectionResult_t PeerConnectionSCTP_CreateDataChannel( PeerConnectionSession_t * pSession,
                                                             char * pcDataChannelName,
                                                             DataChannelInit_t * pDataChannelInit,
                                                             PeerConnectionDataChannel_t ** ppChannel )
{

    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    PeerConnectionDataChannel_t * pChannel;
    uint32_t ulChannelID;
    PeerConnectionDataChannel_t * pxDataChannelIterator = NULL;

    if( ( pSession == NULL ) || ( ppChannel == NULL ) || ( pcDataChannelName == NULL ) )
    {
        LogError( ( "Invalid input." ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {
        pChannel = PeerConnectionSCTP_AllocateDataChannel( &ulChannelID );
        if( pChannel == NULL )
        {
            LogError( ( "No free data channel available input." ) );
            ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
        }
        else if( pSession->uKvsDataChannelCount < PEER_CONNECTION_MAX_SCTP_DATA_CHANNELS_PER_PEER )
        {
            /* Set channel name */
            strncpy( pChannel->ucDataChannelName, pcDataChannelName, MAX_DATA_CHANNEL_NAME_LEN );

            /* Set channel settings */
            if( pDataChannelInit != NULL )
            {
                pChannel->dataChannelInit.negotiated = 0;
                memcpy( &( pChannel->dataChannelInit ), pDataChannelInit, sizeof( DataChannelInit_t ) );
            }
            else
            {
                memset( &( pChannel->dataChannelInit ), 0, sizeof( DataChannelInit_t ) );
                /* Use default */
                pChannel->dataChannelInit.ordered = 1;
                pChannel->dataChannelInit.maxPacketLifeTime.isNull = 1;
                pChannel->dataChannelInit.maxRetransmits.isNull = 1;
            }
            pChannel->channelId = ulChannelID;
            pChannel->pPeerConnection = pSession;

            if( pSession->pDataChannels == NULL )
            {
                /* No other data channels are defined yet - so this is the first in the
                 * list. */
                pChannel->pxNext = NULL;
                pSession->pDataChannels = pChannel;
            }
            else
            {
                pxDataChannelIterator = pSession->pDataChannels;
                for( ; ; )
                {
                    if( pxDataChannelIterator == pChannel )
                    {
                        /* This data channel has already been added to the list. */
                        break;
                    }

                    if( pxDataChannelIterator->pxNext == NULL )
                    {
                        pChannel->pxNext = NULL;
                        pxDataChannelIterator->pxNext = pChannel;
                        break;
                    }

                    pxDataChannelIterator = pxDataChannelIterator->pxNext;
                }
            }
            pSession->uKvsDataChannelCount++;

            *ppChannel = pChannel;
        }
        else
        {
            LogError( ( "Sessions has more than PEER_CONNECTION_MAX_SCTP_DATA_CHANNELS_PER_PEER data channels opened." ) );
            ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
        }
    }

    return ret;
}
/*-----------------------------------------------------------*/

/* Send string data to remote through a given data channel */
PeerConnectionResult_t PeerConnectionSCTP_DataChannelSend( PeerConnectionDataChannel_t * pChannel,
                                                           uint8_t isBinary,
                                                           uint8_t * pMessage,
                                                           uint32_t pMessageLen )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    SCTPSession_t * pSctpSession;


    if( ( pMessage == NULL ) || ( pChannel == NULL ) )
    {
        LogError( ( "No message or pDataChannel received in onDataChannelMessage" ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {

        pSctpSession = &( pChannel->pPeerConnection->sctpSession );

        if( SCTP_WriteMessageSCTPSession( pSctpSession, pChannel->channelId, isBinary, pMessage, pMessageLen ) != SCTP_UTILS_RESULT_OK )
        {
            LogError( ( "SCTP_WriteMessageSCTPSession error" ) );
            ret = PEER_CONNECTION_RESULT_FAIL_SCTP_WRITE;
        }
    }

    return ret;
}
/*-----------------------------------------------------------*/

#if ( DATACHANNEL_CUSTOM_CALLBACK_HOOK == 0 )

/* Default on data channel message callback */
static void onDataChannelMessage( PeerConnectionDataChannel_t * pDataChannel,
                                  uint8_t isBinary,
                                  uint8_t * pMessage,
                                  uint32_t pMessageLen )
{
#define OP_BUFFER_SIZE      512
    char ucSendMessage[OP_BUFFER_SIZE];
    PeerConnectionResult_t retStatus = PEER_CONNECTION_RESULT_OK;
    if( ( pMessage == NULL ) || ( pDataChannel == NULL ) )
    {
        LogError( ( "No message or pDataChannel received in onDataChannelMessage" ) );
        return;
    }

    if( isBinary )
    {
        LogWarn( ( "=============>>>DataChannel Binary Message" ) );
    }
    else {
        LogWarn( ( "=============>>> DataChannel String Message: %.*s\n", ( int ) pMessageLen, pMessage ) );
        /* Send a response to the message sent by the viewer */
        sprintf( ucSendMessage, "Received %ld bytes, ECHO: %.*s", ( long int ) pMessageLen, ( int ) ( pMessageLen > ( OP_BUFFER_SIZE - 128 ) ? ( OP_BUFFER_SIZE - 128 ) : pMessageLen ), pMessage );
        retStatus = PeerConnectionSCTP_DataChannelSend( pDataChannel, 0U, ( uint8_t * ) ucSendMessage, strlen( ucSendMessage ) );
    }

    if( retStatus != PEER_CONNECTION_RESULT_OK )
    {
        LogInfo( ( "[KVS Master] dataChannelSend(): operation returned status code: 0x%08x \n", ( unsigned int ) retStatus ) );
    }

}
/*-----------------------------------------------------------*/

#endif /* (DATACHANNEL_CUSTOM_CALLBACK_HOOK == 0) */

/* Allocate SCTP and initiate session creation */
PeerConnectionResult_t PeerConnectionSCTP_AllocateSCTP( PeerConnectionSession_t * pSession )
{

    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;

    /* Create the SCTP Session */
    pSession->sctpSession.sctpSessionCallbacks.outboundPacketFunc = onSCTPSessionOutboundPacket;
    pSession->sctpSession.sctpSessionCallbacks.dataChannelMessageFunc = onSCTPSessionDataChannelMessage;
    pSession->sctpSession.sctpSessionCallbacks.dataChannelOpenFunc = onSCTPSessionDataChannelOpen;
    pSession->sctpSession.sctpSessionCallbacks.customData = ( void * ) pSession;

    if( SCTP_CreateSCTPSession( &( pSession->sctpSession ) ) == SCTP_UTILS_RESULT_OK )
    {
        uint32_t ulChannelsCreateFailed = 0;
        PeerConnectionDataChannel_t * pxIterator = pSession->pDataChannels;

        /* Create the data channels initialized by the application, if any */
        while( pxIterator != NULL )
        {
            if( SCTP_WriteDCEPSCTPSession( &( pSession->sctpSession ), \
                                           pxIterator->channelId, \
                                           pxIterator->ucDataChannelName, \
                                           ( uint32_t ) strlen( pxIterator->ucDataChannelName ), \
                                           &( pxIterator->dataChannelInit ) ) != SCTP_UTILS_RESULT_OK )
            {
                LogError( ( "Error creating data channel." ) );
                ulChannelsCreateFailed = 1;
            }
            pxIterator = pxIterator->pxNext;
        }

        if( ulChannelsCreateFailed == 0 )
        {
            ret = PEER_CONNECTION_RESULT_OK;
        }

    }


    return ret;

}
/*-----------------------------------------------------------*/

/* Decrypt the incoming DTLS packet and feed it to the SCTP stack. */
void PeerConnectionSCTP_ProcessSCTPData( PeerConnectionSession_t * pSession,
                                         uint8_t * receiveBuffer,
                                         int readBytes )
{

    if( SCTP_PutSCTPPacket( &( pSession->sctpSession ), receiveBuffer, readBytes ) != SCTP_UTILS_RESULT_OK )
    {
        LogWarn( ( "Failed to put SCTP packet" ) );
    }

}
/*-----------------------------------------------------------*/

/* Callback used to send the SCTP outbound packet coming from the SCTP stack
 * This API uses underlying crypto library to encrypt the packet before sending
 * it on the network. */
static void onSCTPSessionOutboundPacket( void * customData,
                                         uint8_t * pPacket,
                                         uint32_t packetLen )
{

    PeerConnectionSession_t * pPeerConnectionSession = NULL;
    if( customData == NULL )
    {
        LogError( ( "No context found" ) );
        return;
    }

    pPeerConnectionSession = ( PeerConnectionSession_t * ) customData;

    if( DTLS_Send( &( pPeerConnectionSession->dtlsSession.xNetworkContext ), pPacket, packetLen ) < 0 )
    {
        LogError( ( "SCTP encrypt error" ) );
    }

}
/*-----------------------------------------------------------*/

/* A message has arrived on the given SCTP session find out the data channel
 * to which its destined to and call the target application provided callback
 * of the channel with the incoming data. */
static void onSCTPSessionDataChannelMessage( void * customData,
                                             uint32_t channelId,
                                             uint8_t isBinary,
                                             uint8_t * pMessage,
                                             uint32_t pMessageLen )
{
    PeerConnectionSession_t * pPeerConnectionSession = ( PeerConnectionSession_t * ) customData;
    PeerConnectionDataChannel_t * pChannel = NULL;
    PeerConnectionDataChannel_t * pxIterator;
    if( customData == NULL )
    {
        LogError( ( "No context found" ) );
        return;
    }

    pxIterator = pPeerConnectionSession->pDataChannels;
    while( pxIterator != NULL )
    {
        if( pxIterator->channelId == channelId )
        {
            pChannel = pxIterator;
            break;
        }
        pxIterator = pxIterator->pxNext;
    }

    if( pChannel != NULL )
    {
        pChannel->onDataChannelMessage( pChannel, isBinary, pMessage, pMessageLen );
    }
    else
    {
        LogError( ( "No channel or message handler found" ) );
    }

}
/*-----------------------------------------------------------*/

/* Callback to allocate and initialise a data channel when there is a valid
 * incoming DCEP DATA_CHANNEL_OPEN Message from the remote. */
static void onSCTPSessionDataChannelOpen( void * customData,
                                          uint32_t channelId,
                                          uint8_t * pName,
                                          uint32_t nameLen )
{
    PeerConnectionSession_t * pPeerConnectionSession = ( PeerConnectionSession_t * ) customData;
    PeerConnectionDataChannel_t * pChannel = NULL;

    if( ( pPeerConnectionSession == NULL ) || ( ( uKVSDataChannelCount >= MAX_SCTP_DATA_CHANNELS ) ) )
    {
        LogError( ( "No context found or not enough data channel remaining" ) );
        return;
    }

    if( ( pPeerConnectionSession->uKvsDataChannelCount < PEER_CONNECTION_MAX_SCTP_DATA_CHANNELS_PER_PEER ) \
        && ( ( pChannel = PeerConnectionSCTP_AllocateDataChannel( NULL ) ) != NULL ) )
    {
        PeerConnectionDataChannel_t * pxIterator = NULL;

        strncpy( ( pChannel->ucDataChannelName ), ( char * ) pName, nameLen );
        pChannel->pPeerConnection = pPeerConnectionSession;
        pChannel->channelId = channelId;

        #if DATACHANNEL_CUSTOM_CALLBACK_HOOK
        {
            pChannel->onDataChannelMessage = PeerConnectionSCTP_SetChannelOneMessageCallbackHook( pPeerConnectionSession, channelId, pName, nameLen );
        }
        #else
        {
            pChannel->onDataChannelMessage = onDataChannelMessage;
        }
        #endif /* DATACHANNEL_CUSTOM_CALLBACK_HOOK */

        if( pPeerConnectionSession->pDataChannels == NULL )
        {
            /* No other data channels are defined yet - so this is the first in the
             * list. */
            pChannel->pxNext = NULL;
            pPeerConnectionSession->pDataChannels = pChannel;
        }
        else
        {
            pxIterator = pPeerConnectionSession->pDataChannels;
            for( ; ; )
            {
                if( pxIterator == pChannel )
                {
                    /* This data channel has already been added to the list. */
                    break;
                }

                if( pxIterator->pxNext == NULL )
                {
                    pChannel->pxNext = NULL;
                    pxIterator->pxNext = pChannel;
                    break;
                }

                pxIterator = pxIterator->pxNext;
            }
        }

        pPeerConnectionSession->uKvsDataChannelCount++;
    }
    else
    {
        LogError( ( "All %d data channel handles are open, no free handles available", MAX_SCTP_DATA_CHANNELS ) );
    }

}
/*-----------------------------------------------------------*/

PeerConnectionResult_t PeerConnectionSCTP_CloseDataChannel( PeerConnectionDataChannel_t * pChannel )
{
    PeerConnectionSession_t * pPeerConnectionSession = pChannel->pPeerConnection;
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( SCTP_StreamReset( &( pPeerConnectionSession->sctpSession ), pChannel->channelId ) != SCTP_UTILS_RESULT_OK )
    {
        ret = PEER_CONNECTION_RESULT_FAIL_SCTP_CLOSE;
    }
    else
    {

        PeerConnectionDataChannel_t * pxIterator = pPeerConnectionSession->pDataChannels;
        PeerConnectionDataChannel_t * pxPrev = NULL;

        while( pxIterator != NULL )
        {
            if( pxIterator == pChannel )
            {
                break;
            }
            pxPrev = pxIterator;
            pxIterator = pxIterator->pxNext;
        }

        if( pxIterator != NULL )
        {
            if( pxPrev == NULL )
            {
                pPeerConnectionSession->pDataChannels = pxIterator->pxNext;
            }
            else
            {
                pxPrev->pxNext = pxIterator->pxNext;
            }

            if( pPeerConnectionSession->uKvsDataChannelCount > 0 )
            {
                pPeerConnectionSession->uKvsDataChannelCount--;
            }
        }

        ret = PeerConnectionSCTP_DeallocateDataChannel( pChannel );
    }
    return ret;
}
/*-----------------------------------------------------------*/

/* Close all open data channels and free the SCTP session */
PeerConnectionResult_t PeerConnectionSCTP_DeallocateSCTP( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    PeerConnectionDataChannel_t * pxIterator = pSession->pDataChannels;

    while( pxIterator != NULL )
    {
        SCTP_StreamReset( &( pSession->sctpSession ), pxIterator->channelId );
        PeerConnectionSCTP_DeallocateDataChannel( pxIterator );
        pxIterator = pxIterator->pxNext;
    }

    pSession->pDataChannels = NULL;
    pSession->uKvsDataChannelCount = 0U;

    if( SCTP_FreeSCTPSession( &( pSession->sctpSession ) ) != SCTP_UTILS_RESULT_OK )
    {
        ret = PEER_CONNECTION_RESULT_FAIL_SCTP_CLOSE;
    }

    return ret;

}
/*-----------------------------------------------------------*/

#endif /* ENABLE_SCTP_DATA_CHANNEL */
