#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <unistd.h>

#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
#include "ice_api.h"
#include "signaling_controller.h"
#include "metric.h"

#define ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_UNKNOWN "UNKNOWN"
#define ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_BINDING_REQUEST "BINDING_REQUEST"
#define ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_BINDING_SUCCESS "BINDING_SUCCESS_RESPONSE"
#define ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_BINDING_FAILURE "BINDING_FAILURE_RESPONSE"
#define ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_BINDING_INDICATION "BINDING_INDICATION"

#define ICE_CONTROLLER_RESEND_DELAY_MS ( 50 )
#define ICE_CONTROLLER_RESEND_TIMEOUT_MS ( 1000 )

static void getLocalIPAdresses( IceEndpoint_t *pLocalIpAddresses, size_t *pLocalIpAddressesNum )
{
    struct ifaddrs *pIfAddrs, *pIfAddr;
    struct sockaddr_in* pIpv4Addr = NULL;
    size_t localIpAddressesSize = *pLocalIpAddressesNum;
    size_t localIpAddressesNum = 0;
    // struct sockaddr_in6* pIpv6Addr = NULL;
    // char ipv6Addr[ 16 ];

    getifaddrs( &pIfAddrs );

    for( pIfAddr = pIfAddrs ; pIfAddr && localIpAddressesNum < localIpAddressesSize ; pIfAddr = pIfAddr->ifa_next )
    {
        if( pIfAddr->ifa_addr && pIfAddr->ifa_addr->sa_family == AF_INET &&
            ( pIfAddr->ifa_flags & IFF_LOOPBACK ) == 0 ) // Ignore loopback interface
        {
            pLocalIpAddresses[ localIpAddressesNum ].transportAddress.family = STUN_ADDRESS_IPv4;
            pLocalIpAddresses[ localIpAddressesNum ].transportAddress.port = 0;
            pIpv4Addr = ( struct sockaddr_in* ) pIfAddr->ifa_addr;
            memcpy( pLocalIpAddresses[ localIpAddressesNum ].transportAddress.address , &pIpv4Addr->sin_addr, STUN_IPV4_ADDRESS_SIZE );
            pLocalIpAddresses[ localIpAddressesNum ].isPointToPoint = ( ( pIfAddr->ifa_flags & IFF_POINTOPOINT ) != 0 );
            localIpAddressesNum++;
        }
        else if (pIfAddr->ifa_addr && pIfAddr->ifa_addr->sa_family == AF_INET6)
        {
            /* TODO: skip IPv6 for now. */
            // getnameinfo( pIfAddr->ifa_addr, sizeof(struct sockaddr_in6), ipv6Addr, sizeof(ipv6Addr), NULL, 0, NI_NUMERICHOST );
            // pLocalIpAddresses[ localIpAddressesNum ].transportAddress.family = STUN_ADDRESS_IPv6;
            // pLocalIpAddresses[ localIpAddressesNum ].transportAddress.port = 0;
            // pIpv6Addr = ( struct sockaddr_in6* ) pIfAddr->ifa_addr;
            // memcpy( pLocalIpAddresses[ localIpAddressesNum ].transportAddress.address , &pIpv6Addr->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
            // pLocalIpAddresses[ localIpAddressesNum ].isPointToPoint = ( ( pIfAddr->ifa_flags & IFF_POINTOPOINT ) != 0 );
            // localIpAddressesNum++;
        }
    }

    *pLocalIpAddressesNum = localIpAddressesNum;

    freeifaddrs( pIfAddrs );
}


static IceControllerResult_t createSocketConnection( int * pSocketFd,
                                                     IceEndpoint_t * pIceEndpoint,
                                                     IceSocketProtocol_t protocol )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    struct sockaddr_in ipv4Address;
    // struct sockaddr_in6 ipv6Addr;
    struct sockaddr * sockAddress = NULL;
    socklen_t addressLength;
    uint32_t socketTimeoutMs = 1U;
    uint32_t sendBufferSize = 0;

    *pSocketFd = socket( pIceEndpoint->transportAddress.family == STUN_ADDRESS_IPv4 ? AF_INET : AF_INET6,
                         protocol == ICE_SOCKET_PROTOCOL_UDP ? SOCK_DGRAM : SOCK_STREAM,
                         0 );

    if( *pSocketFd == -1 )
    {
        LogError( ( "socket() failed to create socket with errno: %s", strerror( errno ) ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_CREATE;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( pIceEndpoint->transportAddress.family == STUN_ADDRESS_IPv4 )
        {
            memset( &ipv4Address, 0, sizeof( ipv4Address ) );
            ipv4Address.sin_family = AF_INET;
            ipv4Address.sin_port = 0; // use next available port
            memcpy( &ipv4Address.sin_addr, pIceEndpoint->transportAddress.address, STUN_IPV4_ADDRESS_SIZE );
            sockAddress = ( struct sockaddr * ) &ipv4Address;
            addressLength = sizeof( struct sockaddr_in );
        }
        else
        {
            /* TODO: skip IPv6 for now. */
            // memset( &ipv6Addr, 0x00, sizeof(ipv6Addr) );
            // ipv6Addr.sin6_family = AF_INET6;
            // ipv6Addr.sin6_port = 0; // use next available port
            // memcpy(&ipv6Addr.sin6_addr, pIceEndpoint->transportAddress.address, STUN_IPV4_ADDRESS_SIZE);
            // sockAddress = (struct sockaddr*) &ipv6Addr;
            // addressLength = sizeof(struct sockaddr_in6);
            ret = ICE_CONTROLLER_RESULT_IPV6_NOT_SUPPORT;
            close( *pSocketFd );
            *pSocketFd = -1;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( bind( *pSocketFd, sockAddress, addressLength ) < 0 )
        {
            LogError( ( "socket() failed to bind socket with errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_BIND;
            close( *pSocketFd );
            *pSocketFd = -1;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        setsockopt( *pSocketFd, SOL_SOCKET, SO_SNDBUF, &sendBufferSize, sizeof( sendBufferSize ) );
        setsockopt( *pSocketFd, SOL_SOCKET, SO_RCVTIMEO, &socketTimeoutMs, sizeof( socketTimeoutMs ) );
        setsockopt( *pSocketFd, SOL_SOCKET, SO_SNDTIMEO, &socketTimeoutMs, sizeof( socketTimeoutMs ) );
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( getsockname( *pSocketFd, sockAddress, &addressLength ) < 0 )
        {
            LogError( ( "getsockname() failed with errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_GETSOCKNAME;
            close( *pSocketFd );
            *pSocketFd = -1;
        }
        else
        {
            pIceEndpoint->transportAddress.port = ( uint16_t ) pIceEndpoint->transportAddress.family == STUN_ADDRESS_IPv4 ? ntohs( ipv4Address.sin_port ) : 0U;
        }
    }

    return ret;
}

void IceControllerNet_FreeSocketContext( IceControllerContext_t * pCtx,
                                         IceControllerSocketContext_t * pSocketContext )
{
    if( pSocketContext && ( pSocketContext->socketFd != -1 ) )
    {
        close( pSocketContext->socketFd );

        pSocketContext->socketFd = -1;
        pSocketContext->state = ICE_CONTROLLER_SOCKET_CONTEXT_STATE_NONE;
    }
}

static void IceControllerNet_AddSrflxCandidate( IceControllerContext_t * pCtx,
                                                IceEndpoint_t * pLocalIceEndpoint )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceResult_t iceResult;
    uint32_t i;
    IceControllerSocketContext_t * pSocketContext;
    uint8_t stunBuffer[ ICE_CONTROLLER_STUN_MESSAGE_BUFFER_SIZE ];
    size_t stunBufferLength = ICE_CONTROLLER_STUN_MESSAGE_BUFFER_SIZE;
    #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE
    char ipBuffer[ INET_ADDRSTRLEN ];
    #endif /* #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE  */

    for( i = 0; i < pCtx->iceServersCount; i++ )
    {
        /* Reset ret for every round. */
        ret = ICE_CONTROLLER_RESULT_OK;

        if( pCtx->iceServers[ i ].serverType != ICE_CONTROLLER_ICE_SERVER_TYPE_STUN )
        {
            /* Not STUN server, no need to create srflx candidate for this server. */
            continue;
        }
        else if( pCtx->iceServers[ i ].iceEndpoint.transportAddress.family != STUN_ADDRESS_IPv4 )
        {
            /* For srflx candidate, we only support IPv4 for now. */
            continue;
        }
        else
        {
            /* Do nothing, coverity happy. */
        }

        /* Only support IPv4 STUN for now. */
        if( ( pCtx->iceServers[ i ].iceEndpoint.transportAddress.family == STUN_ADDRESS_IPv4 ) &&
            ( pLocalIceEndpoint->transportAddress.family == pCtx->iceServers[ i ].iceEndpoint.transportAddress.family ) )
        {
            pSocketContext = &pCtx->socketsContexts[ pCtx->socketsContextsCount ];
            ret = createSocketConnection( &pSocketContext->socketFd, pLocalIceEndpoint, ICE_SOCKET_PROTOCOL_UDP );
            LogVerbose( ( "Create srflx candidate with fd %d, IP/port: %s/%d",
                          pSocketContext->socketFd,
                          IceControllerNet_LogIpAddressInfo( pLocalIceEndpoint, ipBuffer, sizeof( ipBuffer ) ),
                          pLocalIceEndpoint->transportAddress.port ) );
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            iceResult = Ice_AddServerReflexiveCandidate( &pCtx->iceContext,
                                                         pLocalIceEndpoint,
                                                         stunBuffer, &stunBufferLength );
            if( iceResult != ICE_RESULT_OK )
            {
                /* Free resource that already created. */
                LogError( ( "Ice_AddServerReflexiveCandidate fail, result: %d", iceResult ) );
                IceControllerNet_FreeSocketContext( pCtx, pSocketContext );
                ret = ICE_CONTROLLER_RESULT_FAIL_ADD_HOST_CANDIDATE;
                break;
            }
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            pSocketContext->state = ICE_CONTROLLER_SOCKET_CONTEXT_STATE_CREATE;
            pSocketContext->candidateType = ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
            pSocketContext->pLocalCandidate = &pCtx->iceContext.pLocalCandidates[ pCtx->iceContext.numLocalCandidates - 1 ];

            ret = IceControllerNet_SendPacket( pSocketContext, &pCtx->iceServers[ i ].iceEndpoint, stunBuffer, stunBufferLength );
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            pCtx->socketsContextsCount++;
            pCtx->metrics.pendingSrflxCandidateNum++;
        }
    }
}

IceControllerResult_t IceControllerNet_ConvertIpString( const char * pIpAddr,
                                                        size_t ipAddrLength,
                                                        IceEndpoint_t * pDestinationIceEndpoint )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    char ipAddress[ ICE_CONTROLLER_IP_ADDR_STRING_BUFFER_LENGTH + 1 ];

    if( ipAddrLength > ICE_CONTROLLER_IP_ADDR_STRING_BUFFER_LENGTH )
    {
        LogWarn( ( "invalid IP address detected, IP: %.*s",
                   ( int ) ipAddrLength, pIpAddr ) );
        ret = ICE_CONTROLLER_RESULT_IP_BUFFER_TOO_SMALL;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        memcpy( ipAddress, pIpAddr, ipAddrLength );
        ipAddress[ ipAddrLength ] = '\0';

        if( inet_pton( AF_INET, ipAddress, pDestinationIceEndpoint->transportAddress.address ) == 1 )
        {
            pDestinationIceEndpoint->transportAddress.family = STUN_ADDRESS_IPv4;
        }
        else if( inet_pton( AF_INET6, ipAddress, pDestinationIceEndpoint->transportAddress.address ) == 1 )
        {
            pDestinationIceEndpoint->transportAddress.family = STUN_ADDRESS_IPv6;
        }
        else
        {
            ret = ICE_CONTROLLER_RESULT_INVALID_IP_ADDR;
        }
    }

    return ret;
}

IceControllerResult_t IceControllerNet_Htons( uint16_t port,
                                              uint16_t * pOutPort )
{
    *pOutPort = htons( port );

    return ICE_CONTROLLER_RESULT_OK;
}

IceControllerResult_t IceControllerNet_SendPacket( IceControllerSocketContext_t * pSocketContext,
                                                   IceEndpoint_t * pDestinationIceEndpoint,
                                                   const uint8_t * pBuffer,
                                                   size_t length )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    int sentBytes, sendTotalBytes = 0;
    struct sockaddr * pDestinationAddress = NULL;
    struct sockaddr_in ipv4Address;
    struct sockaddr_in6 ipv6Address;
    socklen_t addressLength = 0;
    uint32_t totalDelayMs = 0;
    char ipBuffer[ INET_ADDRSTRLEN ];

    /* Set socket destination address, including IP type (v4/v6), IP address and port. */
    if( pSocketContext->pLocalCandidate->endpoint.transportAddress.family != pDestinationIceEndpoint->transportAddress.family )
    {
        LogWarn( ( "The sending IP family: %d is different from receiving IP family: %d",
                   pSocketContext->pLocalCandidate->endpoint.transportAddress.family,
                   pDestinationIceEndpoint->transportAddress.family ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_SENDTO;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( pDestinationIceEndpoint->transportAddress.family == STUN_ADDRESS_IPv4 )
        {
            memset( &ipv4Address, 0, sizeof( ipv4Address ) );
            ipv4Address.sin_family = AF_INET;
            ipv4Address.sin_port = htons( pDestinationIceEndpoint->transportAddress.port );
            memcpy( &ipv4Address.sin_addr, pDestinationIceEndpoint->transportAddress.address, STUN_IPV4_ADDRESS_SIZE );

            pDestinationAddress = ( struct sockaddr * ) &ipv4Address;
            addressLength = sizeof( ipv4Address );
        }
        else
        {
            memset( &ipv6Address, 0, sizeof( ipv6Address ) );
            ipv6Address.sin6_family = AF_INET6;
            ipv6Address.sin6_port = htons( pDestinationIceEndpoint->transportAddress.port );
            memcpy( &ipv6Address.sin6_addr, pDestinationIceEndpoint->transportAddress.address, STUN_IPV6_ADDRESS_SIZE );

            pDestinationAddress = ( struct sockaddr * ) &ipv6Address;
            addressLength = sizeof( ipv6Address );
        }
    }

    /* Send data */
    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        while( sendTotalBytes < length )
        {
            sentBytes = sendto( pSocketContext->socketFd,
                                pBuffer + sendTotalBytes,
                                length - sendTotalBytes,
                                0,
                                pDestinationAddress,
                                addressLength );
            if( sentBytes < 0 )
            {
                if( ( errno == EAGAIN ) || ( errno == EWOULDBLOCK ) )
                {
                    /* Just retry for these kinds of errno. */
                }
                else if( ( errno == ENOMEM ) || ( errno == ENOSPC ) || ( errno == ENOBUFS ) )
                {
                    usleep( ICE_CONTROLLER_RESEND_DELAY_MS * 1000 );
                    //vTaskDelay( pdMS_TO_TICKS( ICE_CONTROLLER_RESEND_DELAY_MS ) );
                    totalDelayMs += ICE_CONTROLLER_RESEND_DELAY_MS;

                    if( ICE_CONTROLLER_RESEND_TIMEOUT_MS <= totalDelayMs )
                    {
                        LogWarn( ( "Fail to send before timeout: %dms", ICE_CONTROLLER_RESEND_TIMEOUT_MS ) );
                        ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_SENDTO;
                        break;
                    }
                }
                else
                {
                    LogWarn( ( "Failed to send to socket fd: %d error, errno(%d): %s", pSocketContext->socketFd, errno, strerror( errno ) ) );
                    LogWarn( ( "Source family: %d, IP:port: %s:%u",
                               pSocketContext->pLocalCandidate->endpoint.transportAddress.family,
                               IceControllerNet_LogIpAddressInfo( &pSocketContext->pLocalCandidate->endpoint, ipBuffer, sizeof( ipBuffer ) ),
                               pSocketContext->pLocalCandidate->endpoint.transportAddress.port ) );

                    LogWarn( ( "Dest family: %d, IP:port: %s:%u",
                               pDestinationIceEndpoint->transportAddress.family,
                               IceControllerNet_LogIpAddressInfo( pDestinationIceEndpoint, ipBuffer, sizeof( ipBuffer ) ),
                               pDestinationIceEndpoint->transportAddress.port ) );
                    ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_SENDTO;
                    break;
                }
            }
            else
            {
                sendTotalBytes += sentBytes;
            }
        }
    }

    return ret;
}

IceControllerResult_t IceControllerNet_AddLocalCandidates( IceControllerContext_t * pCtx )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceResult_t iceResult;
    uint32_t i;
    IceCandidate_t * pCandidate;
    IceControllerSocketContext_t * pSocketContext;
    int32_t retLocalCandidateReady;
    IceControllerCallbackContent_t localCandidateReadyContent;
    #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE
    char ipBuffer[ INET_ADDRSTRLEN ];
    #endif /* #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE  */

    if( pCtx == NULL )
    {
        LogError( ( "Invalid input, pCtx: %p", pCtx ) );
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Collect information from local network interfaces. */
        pCtx->localIceEndpointsCount = ICE_CONTROLLER_MAX_LOCAL_CANDIDATE_COUNT;
        getLocalIPAdresses( pCtx->localEndpoints, &pCtx->localIceEndpointsCount );

        /* Start gathering local candidates. */
        if( pthread_mutex_lock( &( pCtx->socketMutex ) ) == 0 )
        {
            for( i = 0; i < pCtx->localIceEndpointsCount; i++ )
            {
                pSocketContext = &pCtx->socketsContexts[ pCtx->socketsContextsCount ];
                ret = createSocketConnection( &pSocketContext->socketFd, &pCtx->localEndpoints[i], ICE_SOCKET_PROTOCOL_UDP );

                if( ret == ICE_CONTROLLER_RESULT_OK )
                {
                    iceResult = Ice_AddHostCandidate( &pCtx->iceContext, &pCtx->localEndpoints[i] );
                    if( iceResult != ICE_RESULT_OK )
                    {
                        /* Free resource that already created. */
                        LogError( ( "Ice_AddHostCandidate fail, result: %d", iceResult ) );
                        IceControllerNet_FreeSocketContext( pCtx, pSocketContext );
                        ret = ICE_CONTROLLER_RESULT_FAIL_ADD_HOST_CANDIDATE;
                        break;
                    }
                }

                if( ret == ICE_CONTROLLER_RESULT_OK )
                {
                    pCandidate = &( pCtx->iceContext.pLocalCandidates[ pCtx->iceContext.numLocalCandidates - 1 ] );
                    if( pCtx->onIceEventCallbackFunc )
                    {
                        localCandidateReadyContent.iceControllerCallbackContent.localCandidateReadyMsg.pLocalCandidate = pCandidate;
                        localCandidateReadyContent.iceControllerCallbackContent.localCandidateReadyMsg.localCandidateIndex = pCtx->candidateFoundationCounter;
                        retLocalCandidateReady = pCtx->onIceEventCallbackFunc( pCtx->pOnIceEventCustomContext, ICE_CONTROLLER_CB_EVENT_LOCAL_CANDIDATE_READY, &localCandidateReadyContent );
                        if( retLocalCandidateReady == 0 )
                        {
                            pCtx->candidateFoundationCounter++;
                        }
                        else
                        {
                            /* Free resource that already created. */
                            IceControllerNet_FreeSocketContext( pCtx, pSocketContext );
                            LogError( ( "Fail to send local candidate, ret: %d.", retLocalCandidateReady ) );
                            ret = ICE_CONTROLLER_RESULT_CANDIDATE_SEND_FAIL;
                        }
                    }
                }

                if( ret == ICE_CONTROLLER_RESULT_OK )
                {
                    pSocketContext->state = ICE_CONTROLLER_SOCKET_CONTEXT_STATE_READY;
                    pSocketContext->candidateType = ICE_CANDIDATE_TYPE_HOST;
                    pSocketContext->pLocalCandidate = pCandidate;
                    pCtx->socketsContextsCount++;

                    LogVerbose( ( "Created host candidate with fd %d, IP/port: %s/%d",
                                  pSocketContext->socketFd,
                                  IceControllerNet_LogIpAddressInfo( &pCtx->localEndpoints[i], ipBuffer, sizeof( ipBuffer ) ),
                                  pCtx->localEndpoints[i].transportAddress.port ) );
                }

                /* Prepare srflx candidates based on current host candidate. */
                if( ret == ICE_CONTROLLER_RESULT_OK )
                {
                    IceControllerNet_AddSrflxCandidate( pCtx, &pCtx->localEndpoints[i] );
                }
            }
            /* We have finished accessing the shared resource.  Release the mutex. */
            pthread_mutex_unlock( &( pCtx->socketMutex ) );
        }
        else
        {
            LogError( ( "Fail to take mutex, this is unexpected." ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_MUTEX_TAKE;
        }
    }

    return ret;
}

IceControllerResult_t IceControllerNet_HandleStunPacket( IceControllerContext_t * pCtx,
                                                         IceControllerSocketContext_t * pSocketContext,
                                                         uint8_t * pReceiveBuffer,
                                                         size_t receiveBufferLength,
                                                         IceEndpoint_t * pRemoteIceEndpoint )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceHandleStunPacketResult_t iceHandleStunResult;
    uint8_t * pTransactionIdBuffer;
    IceCandidatePair_t * pCandidatePair = NULL;
    #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE
    char ipBuffer[ INET_ADDRSTRLEN ];
    char ipBuffer2[ INET_ADDRSTRLEN ];
    #endif /* #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE */
    int32_t retLocalCandidateReady;
    IceControllerCallbackContent_t localCandidateReadyContent;
    uint8_t sentStunBuffer[ ICE_CONTROLLER_STUN_MESSAGE_BUFFER_SIZE ];
    size_t sentStunBufferLength = ICE_CONTROLLER_STUN_MESSAGE_BUFFER_SIZE;

    if( ( pCtx == NULL ) || ( pReceiveBuffer == NULL ) || ( pRemoteIceEndpoint == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pReceiveBuffer: %p, pRemoteIceEndpoint: %p",
                    pCtx, pReceiveBuffer, pRemoteIceEndpoint ) );
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        LogVerbose( ( "Receiving %lu bytes from IP/port: %s/%d", receiveBufferLength,
                      IceControllerNet_LogIpAddressInfo( pRemoteIceEndpoint, ipBuffer, sizeof( ipBuffer ) ),
                      pRemoteIceEndpoint->transportAddress.port ) );
        IceControllerNet_LogStunPacket( pReceiveBuffer, receiveBufferLength );

        iceHandleStunResult = Ice_HandleStunPacket( &pCtx->iceContext,
                                                    pReceiveBuffer,
                                                    ( size_t ) receiveBufferLength,
                                                    &pSocketContext->pLocalCandidate->endpoint,
                                                    pRemoteIceEndpoint,
                                                    &pTransactionIdBuffer,
                                                    &pCandidatePair );

        switch( iceHandleStunResult )
        {
            case ICE_HANDLE_STUN_PACKET_RESULT_UPDATED_SERVER_REFLEXIVE_CANDIDATE_ADDRESS:
                if( pCtx->onIceEventCallbackFunc )
                {
                    /* Update socket context. */
                    if( pthread_mutex_lock( &( pCtx->socketMutex ) ) == 0 )
                    {
                        pSocketContext->state = ICE_CONTROLLER_SOCKET_CONTEXT_STATE_READY;

                        /* We have finished accessing the shared resource.  Release the mutex. */
                        pthread_mutex_unlock( &( pCtx->socketMutex ) );
                    }

                    localCandidateReadyContent.iceControllerCallbackContent.localCandidateReadyMsg.pLocalCandidate = pSocketContext->pLocalCandidate;
                    localCandidateReadyContent.iceControllerCallbackContent.localCandidateReadyMsg.localCandidateIndex = pCtx->candidateFoundationCounter;
                    retLocalCandidateReady = pCtx->onIceEventCallbackFunc( pCtx->pOnIceEventCustomContext, ICE_CONTROLLER_CB_EVENT_LOCAL_CANDIDATE_READY, &localCandidateReadyContent );
                    if( retLocalCandidateReady == 0 )
                    {
                        pCtx->candidateFoundationCounter++;
                    }
                    else
                    {
                        /* Free resource that already created. */
                        LogWarn( ( "Fail to send server reflexive candidate to remote peer, ret: %d.", retLocalCandidateReady ) );
                    }
                }

                pCtx->metrics.pendingSrflxCandidateNum--;
                if( pCtx->metrics.pendingSrflxCandidateNum == 0 )
                {
                    Metric_EndEvent( METRIC_EVENT_ICE_GATHER_SRFLX_CANDIDATES );
                }
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_SEND_TRIGGERED_CHECK:
            case ICE_HANDLE_STUN_PACKET_RESULT_SEND_RESPONSE_FOR_NOMINATION:
            case ICE_HANDLE_STUN_PACKET_RESULT_SEND_RESPONSE_FOR_REMOTE_REQUEST:
                if( Ice_CreateResponseForRequest( &pCtx->iceContext,
                                                  pCandidatePair,
                                                  pTransactionIdBuffer,
                                                  sentStunBuffer,
                                                  &sentStunBufferLength ) != ICE_RESULT_OK )
                {
                    LogWarn( ( "Unable to create STUN response for nomination" ) );
                }
                else
                {
                    LogDebug( ( "Sending STUN bind response back to remote" ) );
                    IceControllerNet_LogStunPacket( sentStunBuffer, sentStunBufferLength );

                    if( IceControllerNet_SendPacket( pSocketContext, &pCandidatePair->pRemoteCandidate->endpoint, sentStunBuffer, sentStunBufferLength ) != ICE_CONTROLLER_RESULT_OK )
                    {
                        LogWarn( ( "Unable to send STUN response for nomination" ) );
                    }
                    else
                    {
                        LogDebug( ( "Sent STUN bind response back to remote" ) );
                        if( iceHandleStunResult == ICE_HANDLE_STUN_PACKET_RESULT_SEND_RESPONSE_FOR_NOMINATION )
                        {
                            LogDebug( ( "Sent nominating STUN bind response" ) );
                            LogVerbose( ( "Candidiate pair is nominated, local IP/port: %s/%u, remote IP/port: %s/%u",
                                          IceControllerNet_LogIpAddressInfo( &pCandidatePair->pLocalCandidate->endpoint, ipBuffer, sizeof( ipBuffer ) ), pCandidatePair->pLocalCandidate->endpoint.transportAddress.port,
                                          IceControllerNet_LogIpAddressInfo( &pCandidatePair->pRemoteCandidate->endpoint, ipBuffer2, sizeof( ipBuffer2 ) ), pCandidatePair->pRemoteCandidate->endpoint.transportAddress.port ) );
                            if( TIMER_CONTROLLER_RESULT_SET == TimerController_IsTimerSet( &pCtx->connectivityCheckTimer ) )
                            {
                                TimerController_ResetTimer( &pCtx->connectivityCheckTimer );
                                Metric_EndEvent( METRIC_EVENT_ICE_FIND_P2P_CONNECTION );
                            }

                            /* Update socket context. */
                            if( pthread_mutex_lock( &( pCtx->socketMutex ) ) == 0 )
                            {
                                pCtx->pNominatedSocketContext = pSocketContext;
                                pCtx->pNominatedSocketContext->pRemoteCandidate = pCandidatePair->pRemoteCandidate;

                                /* We have finished accessing the shared resource.  Release the mutex. */
                                pthread_mutex_unlock( &( pCtx->socketMutex ) );
                            }
                            ret = ICE_CONTROLLER_RESULT_FOUND_CONNECTION;
                        }
                    }
                }
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_START_NOMINATION:
                LogInfo( ( "ICE_HANDLE_STUN_PACKET_RESULT_START_NOMINATION" ) );
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_VALID_CANDIDATE_PAIR:
                LogInfo( ( "A valid candidate pair is found" ) );
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_CANDIDATE_PAIR_READY:
                LogInfo( ( "ICE_HANDLE_STUN_PACKET_RESULT_CANDIDATE_PAIR_READY" ) );
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_INTEGRITY_MISMATCH:
                LogWarn( ( "Message Integrity check of the received packet failed" ) );
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_FINGERPRINT_MISMATCH:
                LogWarn( ( "FingerPrint check of the received packet failed" ) );
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_INVALID_PACKET_TYPE:
                LogWarn( ( "Invalid Type of Packet received" ) );
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_CANDIDATE_PAIR_NOT_FOUND:
                LogError( ( "Error : Valid Candidate Pair is not found" ) );
                break;
            case ICE_HANDLE_STUN_PACKET_RESULT_CANDIDATE_NOT_FOUND:
                LogError( ( "Error : Valid Server Reflexive Candidate is not found" ) );
                break;
            default:
                LogWarn( ( "Unknown case: %d", iceHandleStunResult ) );
                break;
        }
    }


    return ret;
}

IceControllerResult_t IceControllerNet_DnsLookUp( char * pUrl,
                                                  IceTransportAddress_t * pIceTransportAddress )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    int dnsResult;
    struct addrinfo * pResult = NULL;
    struct addrinfo * pIterator;
    struct sockaddr_in * ipv4Address;
    struct sockaddr_in6 * ipv6Address;

    if( ( pUrl == NULL ) || ( pIceTransportAddress == NULL ) )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        dnsResult = getaddrinfo( pUrl, NULL, NULL, &pResult );
        if( dnsResult != 0 )
        {
            LogWarn( ( "DNS query failing, url: %s, result: %d", pUrl, dnsResult ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_DNS_QUERY;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        for( pIterator = pResult; pIterator; pIterator = pIterator->ai_next )
        {
            if( pIterator->ai_family == AF_INET )
            {
                ipv4Address = ( struct sockaddr_in * ) pIterator->ai_addr;
                pIceTransportAddress->family = STUN_ADDRESS_IPv4;
                memcpy( pIceTransportAddress->address, &ipv4Address->sin_addr, STUN_IPV4_ADDRESS_SIZE );
                break;
            }
            else if( pIterator->ai_family == AF_INET6 )
            {
                ipv6Address = ( struct sockaddr_in6 * ) pIterator->ai_addr;
                pIceTransportAddress->family = STUN_ADDRESS_IPv6;
                memcpy( pIceTransportAddress->address, &ipv6Address->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
                break;
            }
        }
    }

    if( pResult )
    {
        freeaddrinfo( pResult );
    }

    return ret;
}

#if LIBRARY_LOG_LEVEL >= LOG_INFO
const char * IceControllerNet_LogIpAddressInfo( const IceEndpoint_t * pIceEndpoint,
                                                char * pIpBuffer,
                                                size_t ipBufferLength )
{
    const char * ret = ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_UNKNOWN;

    if( ( pIceEndpoint != NULL ) && ( pIpBuffer != NULL ) && ( ipBufferLength >= INET_ADDRSTRLEN ) )
    {
        ret = inet_ntop( AF_INET, pIceEndpoint->transportAddress.address, pIpBuffer, ipBufferLength );
    }

    return ret;
}
#endif /* #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE */

#if LIBRARY_LOG_LEVEL >= LOG_VERBOSE

#define SWAP_BYTES_16( value )          \
    ( ( ( ( value ) >> 8 ) & 0xFF ) |   \
      ( ( ( value ) & 0xFF ) << 8 ) )

static uint16_t ReadUint16Swap( const uint8_t * pSrc )
{
    return SWAP_BYTES_16( *( ( uint16_t * )( pSrc ) ) );
}

static uint16_t ReadUint16NoSwap( const uint8_t * pSrc )
{
    return *( ( uint16_t * )( pSrc ) );
}

static const char * convertStunMsgTypeToString( uint16_t stunMsgType )
{
    const char * ret = ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_UNKNOWN;
    static ReadUint16_t readUint16Fn;
    static uint8_t isFirst = 1;
    uint8_t isLittleEndian;
    uint16_t msgType;

    if( isFirst )
    {
        isFirst = 0;
        isLittleEndian = ( *( uint8_t * )( &( uint16_t ) { 1 } ) == 1 );

        if( isLittleEndian != 0 )
        {
            readUint16Fn = ReadUint16Swap;
        }
        else
        {
            readUint16Fn = ReadUint16NoSwap;
        }
    }

    msgType = readUint16Fn( ( uint8_t * ) &stunMsgType );
    switch( msgType )
    {
        case STUN_MESSAGE_TYPE_BINDING_REQUEST:
            ret = ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_BINDING_REQUEST;
            break;
        case STUN_MESSAGE_TYPE_BINDING_SUCCESS_RESPONSE:
            ret = ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_BINDING_SUCCESS;
            break;
        case STUN_MESSAGE_TYPE_BINDING_FAILURE_RESPONSE:
            ret = ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_BINDING_FAILURE;
            break;
        case STUN_MESSAGE_TYPE_BINDING_INDICATION:
            ret = ICE_CONTROLLER_STUN_MESSAGE_TYPE_STRING_BINDING_INDICATION;
            break;
    }

    return ret;
}
#endif /* #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE */

void IceControllerNet_LogStunPacket( uint8_t * pStunPacket,
                                     size_t stunPacketSize )
{
    #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE
    IceControllerStunMsgHeader_t * pStunMsgHeader = ( IceControllerStunMsgHeader_t * ) pStunPacket;

    if( ( pStunPacket == NULL ) || ( stunPacketSize < sizeof( IceControllerStunMsgHeader_t ) ) )
    {
        // invalid STUN packet, ignore it
    }
    else
    {
        LogVerbose( ( "Dumping STUN packets: STUN type: %s, content length:: 0x%02x%02x, transaction ID: 0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
                      convertStunMsgTypeToString( pStunMsgHeader->msgType ),
                      pStunMsgHeader->contentLength[0], pStunMsgHeader->contentLength[1],
                      pStunMsgHeader->transactionId[0], pStunMsgHeader->transactionId[1], pStunMsgHeader->transactionId[2], pStunMsgHeader->transactionId[3],
                      pStunMsgHeader->transactionId[4], pStunMsgHeader->transactionId[5], pStunMsgHeader->transactionId[6], pStunMsgHeader->transactionId[7],
                      pStunMsgHeader->transactionId[8], pStunMsgHeader->transactionId[9], pStunMsgHeader->transactionId[10], pStunMsgHeader->transactionId[11] ) );
    }
    #endif /* #if LIBRARY_LOG_LEVEL >= LOG_DEBUG  */

    ( void ) pStunPacket;
    ( void ) stunPacketSize;
}
