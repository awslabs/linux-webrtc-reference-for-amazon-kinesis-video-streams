#include <errno.h>
#include <unistd.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
#include "ice_api.h"
#include "stun_deserializer.h"
#include "transport_mbedtls.h"

#define ICE_CONTROLLER_SOCKET_LISTENER_SELECT_BLOCK_TIME_MS ( 50 )
#define RX_BUFFER_SIZE ( 4096 )

static int32_t RecvPacketUdp( IceControllerSocketContext_t * pSocketContext,
                              uint8_t * pBuffer,
                              size_t bufferSize,
                              int flags,
                              IceEndpoint_t * pRemoteEndpoint )
{
    int32_t ret;
    struct sockaddr_storage srcAddress;
    socklen_t srcAddressLength = sizeof( srcAddress );
    struct sockaddr_in * pIpv4Address;
    struct sockaddr_in6 * pIpv6Address;
    uint8_t keepProcess = 1U;

    ret = recvfrom( pSocketContext->socketFd, pBuffer, bufferSize, flags, ( struct sockaddr * ) &srcAddress, &srcAddressLength );

    if( ret < 0 )
    {
        if( ( errno == EAGAIN ) || ( errno == EWOULDBLOCK ) )
        {
            /* Timeout, no more data to receive. */
            ret = 0;
            keepProcess = 0U;
        }
    }
    else if( ret == 0 )
    {
        /* Nothing to do if receive 0 byte. */
        keepProcess = 0U;
    }
    else
    {
        /* Empty else marker. */
    }

    if( keepProcess != 0U )
    {
        /* Received data, handle this STUN message. */
        if( srcAddress.ss_family == AF_INET )
        {
            pIpv4Address = ( struct sockaddr_in * ) &srcAddress;

            pRemoteEndpoint->transportAddress.family = STUN_ADDRESS_IPv4;
            pRemoteEndpoint->transportAddress.port = ntohs( pIpv4Address->sin_port );
            memcpy( pRemoteEndpoint->transportAddress.address, &pIpv4Address->sin_addr, STUN_IPV4_ADDRESS_SIZE );
        }
        else if( srcAddress.ss_family == AF_INET6 )
        {
            pIpv6Address = ( struct sockaddr_in6 * ) &srcAddress;

            pRemoteEndpoint->transportAddress.family = STUN_ADDRESS_IPv6;
            pRemoteEndpoint->transportAddress.port = ntohs( pIpv6Address->sin6_port );
            memcpy( pRemoteEndpoint->transportAddress.address, &pIpv6Address->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
        }
        else
        {
            /* Unknown IP type, drop packet. */
            LogWarn( ( "Unknown source type(%d) from UDP connection.", srcAddress.ss_family ) );
            ret = -1;
        }
    }

    return ret;
}

static int32_t RecvPacketTls( IceControllerSocketContext_t * pSocketContext,
                              uint8_t * pBuffer,
                              size_t bufferSize,
                              IceEndpoint_t * pRemoteEndpoint )
{
    int32_t ret;

    memcpy( pRemoteEndpoint, pSocketContext->pIceServerEndpoint, sizeof( IceEndpoint_t ) );
    ret = TLS_FreeRTOS_recv( &pSocketContext->tlsSession.xTlsNetworkContext,
                             pBuffer,
                             bufferSize );

    if( ret < 0 )
    {
        LogError( ( "Receiving %d from TLS connection", ret ) );
    }

    return ret;
}

static void ReleaseOtherSockets( IceControllerContext_t * pCtx,
                                 IceControllerSocketContext_t * pChosenSocketContext )
{
    uint8_t skipProcess = 0;
    int i;

    if( ( pCtx == NULL ) || ( pChosenSocketContext == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pChosenSocketContext: %p", pCtx, pChosenSocketContext ) );
        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        LogDebug( ( "Closing sockets other than: %d", pChosenSocketContext->socketFd ) );
        for( i = 0; i < pCtx->socketsContextsCount; i++ )
        {
            if( pCtx->socketsContexts[i].socketFd != pChosenSocketContext->socketFd )
            {
                if( ( pCtx->socketsContexts[i].pLocalCandidate != NULL ) && ( pCtx->socketsContexts[i].pLocalCandidate->candidateType == ICE_CANDIDATE_TYPE_RELAY ) )
                {
                    /* If the local candidate is a relay candidate, we have to send refresh request with lifetime 0 to end the session.
                     * Thus keep the socket alive until it's terminated. */
                    Ice_CloseCandidate( &pCtx->iceContext,
                                        pCtx->socketsContexts[i].pLocalCandidate );
                }
                else
                {
                    /* Release all unused socket contexts. */
                    LogDebug( ( "Closing socket: %d", pCtx->socketsContexts[i].socketFd ) );
                    IceControllerNet_FreeSocketContext( pCtx, &pCtx->socketsContexts[i] );
                }
            }
        }
    }

    if( skipProcess == 0 )
    {
        IceController_CloseOtherCandidatePairs( pCtx, pChosenSocketContext->pCandidatePair );
    }
}

static void HandleRxPacket( IceControllerContext_t * pCtx,
                            IceControllerSocketContext_t * pSocketContext,
                            OnRecvNonStunPacketCallback_t onRecvNonStunPacketFunc,
                            void * pOnRecvNonStunPacketCallbackContext,
                            OnIceEventCallback_t onIceEventCallbackFunc,
                            void * pOnIceEventCallbackCustomContext )
{
    uint8_t skipProcess = 0;
    int32_t readBytes;
    IceEndpoint_t remoteIceEndpoint;
    IceControllerResult_t ret;
    int32_t retPeerToPeerConnectionFound;
    IceResult_t iceResult;
    IceCandidatePair_t * pCandidatePair = NULL;
    uint8_t * pTurnPayloadBuffer = NULL;
    uint16_t turnPayloadBufferLength = 0;
    uint8_t receiveBuffer[ RX_BUFFER_SIZE ];
    uint8_t * pProcessingBuffer = receiveBuffer;
    size_t processingBufferLength = 0;

    if( ( pCtx == NULL ) || ( pSocketContext == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pSocketContext: %p", pCtx, pSocketContext ) );
        skipProcess = 1;
    }

    while( !skipProcess )
    {
        if( pSocketContext->socketType == ICE_CONTROLLER_SOCKET_TYPE_UDP )
        {
            readBytes = RecvPacketUdp( pSocketContext, pProcessingBuffer, RX_BUFFER_SIZE, 0, &remoteIceEndpoint );
        }
        else if( pSocketContext->socketType == ICE_CONTROLLER_SOCKET_TYPE_TLS )
        {
            readBytes = RecvPacketTls( pSocketContext, pProcessingBuffer, RX_BUFFER_SIZE, &remoteIceEndpoint );
        }
        else
        {
            LogError( ( "Internal error, invalid socket type %d", pSocketContext->socketType ) );
            skipProcess = 1;
            break;
        }

        if( readBytes < 0 )
        {
            LogError( ( "Fail to receive packets from socket ID: %d, errno: %s", pSocketContext->socketFd, strerror( errno ) ) );
            break;
        }
        else if( readBytes == 0 )
        {
            /* Nothing to do if receive 0 byte. */
            break;
        }
        else
        {
            /* Received valid data, keep addressing. */
            LogDebug( ( "Receiving %d btyes from network.", readBytes ) );
            processingBufferLength = ( size_t ) readBytes;
        }

        if( pSocketContext->pLocalCandidate->candidateType == ICE_CANDIDATE_TYPE_RELAY )
        {
            iceResult = Ice_HandleTurnPacket( &pCtx->iceContext,
                                              pSocketContext->pLocalCandidate,
                                              pProcessingBuffer,
                                              processingBufferLength,
                                              ( const uint8_t ** ) &pTurnPayloadBuffer,
                                              &turnPayloadBufferLength,
                                              &pCandidatePair );
            if( ( iceResult != ICE_RESULT_OK ) && ( iceResult != ICE_RESULT_TURN_PREFIX_NOT_REQUIRED ) )
            {
                LogError( ( "Ice_HandleTurnPacket detects failure, iceResult: %d", iceResult ) );
                break;
            }
            else if( iceResult == ICE_RESULT_OK )
            {
                /* Received TURN buffer, replace buffer pointer for further processing. */
                LogInfo( ( "Removed TURN channel header, number: 0x%02x%02x, length: 0x%02x%02x",
                           pProcessingBuffer[0], pProcessingBuffer[1],
                           pProcessingBuffer[2], pProcessingBuffer[3] ) );
                pProcessingBuffer = pTurnPayloadBuffer;
                processingBufferLength = turnPayloadBufferLength;
            }
            else
            {
                /* TURN prefix not required, keep original buffer. */
            }
        }

        /*
         * demux each packet off of its first byte
         * https://tools.ietf.org/html/rfc5764#section-5.1.2
         * +----------------+
         * | 127 < B < 192 -+--> forward to RTP/RTCP
         * |                |
         * |  19 < B < 64  -+--> forward to DTLS
         * |                |
         * |       B < 2   -+--> forward to STUN
         * +----------------+
         */
        ret = IceControllerNet_HandleStunPacket( pCtx,
                                                 pSocketContext,
                                                 pProcessingBuffer,
                                                 processingBufferLength,
                                                 &remoteIceEndpoint,
                                                 pCandidatePair );
        if( ret == ICE_CONTROLLER_RESULT_NOT_STUN_PACKET )
        {
            /* It's not STUN packet, deliever to peer connection to handle RTP or DTLS packet. */
            if( onRecvNonStunPacketFunc )
            {
                ( void ) onRecvNonStunPacketFunc( pOnRecvNonStunPacketCallbackContext, pProcessingBuffer, processingBufferLength );
            }
            else
            {
                LogError( ( "No callback function to handle DTLS/RTP/RTCP packets." ) );
            }
        }
        else if( ( ret == ICE_CONTROLLER_RESULT_FOUND_CONNECTION ) && ( pCtx->pNominatedSocketContext->state != ICE_CONTROLLER_SOCKET_CONTEXT_STATE_SELECTED ) )
        {
            /* Set state to selected and release other un-selected sockets. */
            IceController_UpdateState( pCtx, ICE_CONTROLLER_STATE_READY );
            IceController_UpdateTimerInterval( pCtx, ICE_CONTROLLER_PERIODIC_TIMER_INTERVAL_MS );
            pCtx->pNominatedSocketContext->state = ICE_CONTROLLER_SOCKET_CONTEXT_STATE_SELECTED;

            ReleaseOtherSockets( pCtx, pSocketContext );
            LogDebug( ( "Released all other socket contexts" ) );

            /* Found nominated pair, execute DTLS handshake and release all other resources. */
            if( onIceEventCallbackFunc )
            {
                retPeerToPeerConnectionFound = onIceEventCallbackFunc( pOnIceEventCallbackCustomContext, ICE_CONTROLLER_CB_EVENT_PEER_TO_PEER_CONNECTION_FOUND, NULL );
                if( retPeerToPeerConnectionFound != 0 )
                {
                    LogError( ( "Fail to handle peer to peer connection found event, ret: %d", retPeerToPeerConnectionFound ) );
                }
            }
            else
            {
                LogWarn( ( "No callback function to handle P2P connection found event." ) );
            }
        }
        else if( ( ret == ICE_CONTROLLER_RESULT_FOUND_CONNECTION ) || ( ret == ICE_CONTROLLER_RESULT_OK ) )
        {
            /* Handle STUN packet successfully, keep processing. */
        }
        else if( ret == ICE_CONTROLLER_RESULT_CONNECTION_CLOSED )
        {
            /* Socket has been closed, skip the next recv loop. */
            break;
        }
        else
        {
            LogError( ( "Fail to handle this RX packet, ret: %d, readBytes: %lu", ret, processingBufferLength ) );
        }
    }
}

static void pollingSockets( IceControllerContext_t * pCtx )
{
    fd_set rfds;
    int i;
    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = ICE_CONTROLLER_SOCKET_LISTENER_SELECT_BLOCK_TIME_MS * 1000,
    };
    int maxFd = 0;
    int retSelect;
    uint8_t skipProcess = 0;
    int fds[ ICE_CONTROLLER_MAX_LOCAL_CANDIDATE_COUNT ];
    size_t fdsCount;
    OnRecvNonStunPacketCallback_t onRecvNonStunPacketFunc;
    void * pOnRecvNonStunPacketCallbackContext = NULL;
    OnIceEventCallback_t onIceEventCallbackFunc;
    void * pOnIceEventCallbackCustomContext = NULL;

    FD_ZERO( &rfds );

    if( pthread_mutex_lock( &( pCtx->socketMutex ) ) == 0 )
    {
        for( i = 0; i < pCtx->socketsContextsCount; i++ )
        {
            fds[i] = pCtx->socketsContexts[i].socketFd;
        }
        fdsCount = pCtx->socketsContextsCount;
        onRecvNonStunPacketFunc = pCtx->socketListenerContext.onRecvNonStunPacketFunc;
        pOnRecvNonStunPacketCallbackContext = pCtx->socketListenerContext.pOnRecvNonStunPacketCallbackContext;
        onIceEventCallbackFunc = pCtx->onIceEventCallbackFunc;
        pOnIceEventCallbackCustomContext = pCtx->pOnIceEventCustomContext;

        /* We have finished accessing the shared resource.  Release the mutex. */
        pthread_mutex_unlock( &( pCtx->socketMutex ) );
    }
    else
    {
        LogError( ( "Unexpected behavior: fail to take mutex" ) );
        skipProcess = 1;
    }

    if( !skipProcess )
    {
        /* Set rfds for select function. */
        for( i = 0; i < fdsCount; i++ )
        {
            /* fds might be removed for any reason. Handle that by checking if it's -1. */
            if( fds[i] >= 0 )
            {
                FD_SET( fds[i], &rfds );
                if( fds[i] > maxFd )
                {
                    maxFd = fds[i];
                }
            }
        }

        /* Poll all socket handlers. */
        retSelect = select( maxFd + 1, &rfds, NULL, NULL, &tv );
        if( retSelect < 0 )
        {
            LogError( ( "select return error value %d", retSelect ) );
            skipProcess = 1;
        }
        else if( retSelect == 0 )
        {
            /* It's just timeout. */
            skipProcess = 1;
        }
        else
        {
            /* Empty else marker. */
        }
    }

    if( !skipProcess )
    {
        for( i = 0; i < fdsCount; i++ )
        {
            if( ( fds[i] >= 0 ) && FD_ISSET( fds[i], &rfds ) )
            {
                LogVerbose( ( "Detect packets on fd %d, idx: %d", fds[i], i ) );

                HandleRxPacket( pCtx,
                                &pCtx->socketsContexts[i],
                                onRecvNonStunPacketFunc,
                                pOnRecvNonStunPacketCallbackContext,
                                onIceEventCallbackFunc,
                                pOnIceEventCallbackCustomContext );
            }
        }
    }
}

IceControllerResult_t IceControllerSocketListener_StartPolling( IceControllerContext_t * pCtx )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( pthread_mutex_lock( &( pCtx->socketMutex ) ) == 0 )
    {
        pCtx->socketListenerContext.executeSocketListener = 1;

        /* We have finished accessing the shared resource.  Release the mutex. */
        pthread_mutex_unlock( &( pCtx->socketMutex ) );

        LogDebug( ( "Socket Listener: start polling" ) );
    }
    else
    {
        LogError( ( "Unexpected behavior: fail to take mutex" ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_MUTEX_TAKE;
    }

    return ret;
}

IceControllerResult_t IceControllerSocketListener_StopPolling( IceControllerContext_t * pCtx )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( pthread_mutex_lock( &( pCtx->socketMutex ) ) == 0 )
    {
        pCtx->socketListenerContext.executeSocketListener = 0;

        /* We have finished accessing the shared resource.  Release the mutex. */
        pthread_mutex_unlock( &( pCtx->socketMutex ) );

        LogDebug( ( "Socket Listener: stop polling" ) );
    }
    else
    {
        LogError( ( "Unexpected behavior: fail to take mutex" ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_MUTEX_TAKE;
    }

    return ret;
}

IceControllerResult_t IceControllerSocketListener_Init( IceControllerContext_t * pCtx,
                                                        OnRecvNonStunPacketCallback_t onRecvNonStunPacketFunc,
                                                        void * pOnRecvNonStunPacketCallbackContext )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( pCtx == NULL )
    {
        LogError( ( "Invalid input: pCtx is NULL" ) );
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        pCtx->socketListenerContext.executeSocketListener = 0;
        pCtx->socketListenerContext.onRecvNonStunPacketFunc = onRecvNonStunPacketFunc;
        pCtx->socketListenerContext.pOnRecvNonStunPacketCallbackContext = pOnRecvNonStunPacketCallbackContext;
    }

    return ret;
}

void * IceControllerSocketListener_Task( void * pParameter )
{
    IceControllerContext_t * pCtx = ( IceControllerContext_t * ) pParameter;

    for( ;; )
    {
        while( pCtx->socketListenerContext.executeSocketListener == 0 )
        {
            usleep( ICE_CONTROLLER_SOCKET_LISTENER_SELECT_BLOCK_TIME_MS * 1000 );
            //vTaskDelay( pdMS_TO_TICKS( ICE_CONTROLLER_SOCKET_LISTENER_SELECT_BLOCK_TIME_MS ) );
        }

        if( pCtx->socketListenerContext.executeSocketListener == 1 )
        {
            pollingSockets( pCtx );
        }
    }
}

