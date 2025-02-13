#include <errno.h>
#include <unistd.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
//#include "task.h"
#include "stun_deserializer.h"

#define ICE_CONTROLLER_SOCKET_LISTENER_QUEUE_NAME "/WebrtcApplicationIceControllerSocketListener"
#define MAX_QUEUE_MSG_NUM ( 10 )
#define ICE_CONTROLLER_SOCKET_LISTENER_SELECT_BLOCK_TIME_MS ( 50 )
#define RX_BUFFER_SIZE ( 4096 )

uint8_t receiveBuffer[ RX_BUFFER_SIZE ];

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
                /* Release all unused socket contexts. */
                LogDebug( ( "Closing socket: %d", pCtx->socketsContexts[i].socketFd ) );
                IceControllerNet_FreeSocketContext( pCtx, &pCtx->socketsContexts[i] );
            }
        }
    }
}

static void HandleRxPacket( IceControllerContext_t * pCtx,
                            IceControllerSocketContext_t * pSocketContext,
                            OnRecvDtlsPacketCallback_t onRecvDtlsPacketCallbackFunc,
                            void * pOnRecvDtlsPacketCallbackContext,
                            OnIceEventCallback_t onIceEventCallbackFunc,
                            void * pOnIceEventCallbackCustomContext )
{
    uint8_t skipProcess = 0;
    int readBytes;
    struct sockaddr_storage srcAddress;
    socklen_t srcAddressLength = sizeof( srcAddress );
    struct sockaddr_in * pIpv4Address;
    struct sockaddr_in6 * pIpv6Address;
    IceEndpoint_t remoteIceEndpoint;
    IceControllerResult_t ret;
    StunResult_t retStun;
    StunContext_t stunContext;
    StunHeader_t stunHeader;
    int32_t retPeerToPeerConnectionFound;

    if( ( pCtx == NULL ) || ( pSocketContext == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pSocketContext: %p", pCtx, pSocketContext ) );
        skipProcess = 1;
    }

    while( !skipProcess )
    {
        readBytes = recvfrom( pSocketContext->socketFd, receiveBuffer, RX_BUFFER_SIZE, 0, ( struct sockaddr * ) &srcAddress, &srcAddressLength );
        if( readBytes < 0 )
        {
            if( ( errno == EAGAIN ) || ( errno == EWOULDBLOCK ) )
            {
                /* Timeout, no more data to receive. */
            }
            else
            {
                LogError( ( "Fail to receive packets from socket ID: %d, errno: %s", pSocketContext->socketFd, strerror( errno ) ) );
            }
            skipProcess = 1;
            break;
        }
        else if( readBytes == 0 )
        {
            /* Nothing to do if receive 0 byte. */
            skipProcess = 1;
            break;
        }
        else
        {
            /* Received valid data, keep addressing. */
        }

        /* Received data, handle this STUN message. */
        if( srcAddress.ss_family == AF_INET )
        {
            pIpv4Address = ( struct sockaddr_in * ) &srcAddress;

            remoteIceEndpoint.transportAddress.family = STUN_ADDRESS_IPv4;
            remoteIceEndpoint.transportAddress.port = ntohs( pIpv4Address->sin_port );
            memcpy( remoteIceEndpoint.transportAddress.address, &pIpv4Address->sin_addr, STUN_IPV4_ADDRESS_SIZE );
        }
        else if( srcAddress.ss_family == AF_INET6 )
        {
            pIpv6Address = ( struct sockaddr_in6 * ) &srcAddress;

            remoteIceEndpoint.transportAddress.family = STUN_ADDRESS_IPv6;
            remoteIceEndpoint.transportAddress.port = ntohs( pIpv6Address->sin6_port );
            memcpy( remoteIceEndpoint.transportAddress.address, &pIpv6Address->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
        }
        else
        {
            /* Unknown IP type, drop packet. */
            LogWarn( ( "Unknown IP type: %d", srcAddress.ss_family ) );
            skipProcess = 1;
            break;
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
        retStun = StunDeserializer_Init( &stunContext,
                                         receiveBuffer,
                                         readBytes,
                                         &stunHeader );
        if( retStun != STUN_RESULT_OK )
        {
            /* It's not STUN packet, deliever to peer connection to handle RTP or DTLS packet. */
            if( onRecvDtlsPacketCallbackFunc )
            {
                ( void ) onRecvDtlsPacketCallbackFunc( pOnRecvDtlsPacketCallbackContext, receiveBuffer, readBytes );
            }
            else
            {
                LogError( ( "No callback function to handle DTLS/RTP/RTCP packets." ) );
            }
        }
        else
        {
            ret = IceControllerNet_HandleStunPacket( pCtx,
                                                     pSocketContext,
                                                     receiveBuffer,
                                                     readBytes,
                                                     &remoteIceEndpoint );
            if( ( ret == ICE_CONTROLLER_RESULT_FOUND_CONNECTION ) && ( pCtx->pNominatedSocketContext->state != ICE_CONTROLLER_SOCKET_CONTEXT_STATE_SELECTED ) )
            {
                /* Set state to pass handshake and release other un-selected sockets. */
                pCtx->pNominatedSocketContext->state = ICE_CONTROLLER_SOCKET_CONTEXT_STATE_SELECTED;

                ReleaseOtherSockets( pCtx, pSocketContext );

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
                LogDebug( ( "Released all other socket contexts" ) );
            }
            else if( ( ret == ICE_CONTROLLER_RESULT_FOUND_CONNECTION ) || ( ret == ICE_CONTROLLER_RESULT_OK ) )
            {
                /* Handle STUN packet successfully, keep processing. */
            }
            else
            {
                LogError( ( "Fail to handle this RX packet, ret: %d, readBytes: %d", ret, readBytes ) );
            }
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
    OnRecvDtlsPacketCallback_t onRecvDtlsPacketCallbackFunc;
    void * pOnRecvDtlsPacketCallbackContext = NULL;
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
        onRecvDtlsPacketCallbackFunc = pCtx->socketListenerContext.onRecvDtlsPacketCallbackFunc;
        pOnRecvDtlsPacketCallbackContext = pCtx->socketListenerContext.pOnRecvDtlsPacketCallbackContext;
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

                HandleRxPacket( pCtx, &pCtx->socketsContexts[i],
                                onRecvDtlsPacketCallbackFunc, pOnRecvDtlsPacketCallbackContext,
                                onIceEventCallbackFunc, pOnIceEventCallbackCustomContext );
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
                                                        OnRecvDtlsPacketCallback_t onRecvDtlsPacketCallbackFunc,
                                                        void * pOnRecvDtlsPacketCallbackContext )
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
        pCtx->socketListenerContext.onRecvDtlsPacketCallbackFunc = onRecvDtlsPacketCallbackFunc;
        pCtx->socketListenerContext.pOnRecvDtlsPacketCallbackContext = pOnRecvDtlsPacketCallbackContext;
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
