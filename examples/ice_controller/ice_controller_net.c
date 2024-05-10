#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <errno.h>
#include <netdb.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
#include "ice_api.h"
#include "signaling_controller.h"

#define RX_BUFFER_SIZE ( 4096 )

uint8_t receiveBuffer[ RX_BUFFER_SIZE ];

static void getLocalIPAdresses( IceIPAddress_t *pLocalIpAddresses, size_t *pLocalIpAddressesNum )
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
        if( pIfAddr->ifa_addr && pIfAddr->ifa_addr->sa_family == AF_INET ) 
        {
            pLocalIpAddresses[ localIpAddressesNum ].ipAddress.family = STUN_ADDRESS_IPv4;
            pLocalIpAddresses[ localIpAddressesNum ].ipAddress.port = 0;
            pIpv4Addr = ( struct sockaddr_in* ) pIfAddr->ifa_addr;
            memcpy( pLocalIpAddresses[ localIpAddressesNum ].ipAddress.address , &pIpv4Addr->sin_addr, STUN_IPV4_ADDRESS_SIZE );
            pLocalIpAddresses[ localIpAddressesNum ].isPointToPoint = ( ( pIfAddr->ifa_flags & IFF_POINTOPOINT ) != 0 );
            localIpAddressesNum++;
        }
        else if (pIfAddr->ifa_addr && pIfAddr->ifa_addr->sa_family == AF_INET6)
        {
            /* TODO: skip IPv6 for now. */
            // getnameinfo( pIfAddr->ifa_addr, sizeof(struct sockaddr_in6), ipv6Addr, sizeof(ipv6Addr), NULL, 0, NI_NUMERICHOST );
            // pLocalIpAddresses[ localIpAddressesNum ].ipAddress.family = STUN_ADDRESS_IPv6;
            // pLocalIpAddresses[ localIpAddressesNum ].ipAddress.port = 0;
            // pIpv6Addr = ( struct sockaddr_in6* ) pIfAddr->ifa_addr;
            // memcpy( pLocalIpAddresses[ localIpAddressesNum ].ipAddress.address , &pIpv6Addr->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
            // pLocalIpAddresses[ localIpAddressesNum ].isPointToPoint = ( ( pIfAddr->ifa_flags & IFF_POINTOPOINT ) != 0 );
            // localIpAddressesNum++;
        }
    }

    *pLocalIpAddressesNum = localIpAddressesNum;

    freeifaddrs( pIfAddrs );
}

static IceControllerResult_t createSocketConnection( int *pSocketFd, IceIPAddress_t *pIpAddress, IceSocketProtocol_t protocol )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    struct sockaddr_in ipv4Address;
    // struct sockaddr_in6 ipv6Addr;
    struct sockaddr* sockAddress = NULL;
    socklen_t addressLength;

    *pSocketFd = socket( pIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 ? AF_INET : AF_INET6,
                         protocol == ICE_SOCKET_PROTOCOL_UDP? SOCK_DGRAM : SOCK_STREAM,
                         0 );

    if( *pSocketFd == -1 ) 
    {
        LogError( ( "socket() failed to create socket with errno: %s", strerror( errno ) ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_CREATE;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( pIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 )
        {
            memset( &ipv4Address, 0x00, sizeof(ipv4Address) );
            ipv4Address.sin_family = AF_INET;
            ipv4Address.sin_port = 0; // use next available port
            memcpy( &ipv4Address.sin_addr, pIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE );
            sockAddress = (struct sockaddr*) &ipv4Address;
            addressLength = sizeof(struct sockaddr_in);
        }
        else
        {
            /* TODO: skip IPv6 for now. */
            // memset( &ipv6Addr, 0x00, sizeof(ipv6Addr) );
            // ipv6Addr.sin6_family = AF_INET6;
            // ipv6Addr.sin6_port = 0; // use next available port
            // memcpy(&ipv6Addr.sin6_addr, pHostIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE);
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
        if( getsockname( *pSocketFd, sockAddress, &addressLength ) < 0 )
        {
            LogError( ( "getsockname() failed with errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_GETSOCKNAME;
            close( *pSocketFd );
            *pSocketFd = -1;
        }
        else
        {
            pIpAddress->ipAddress.port = ( uint16_t ) pIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 ? ipv4Address.sin_port : 0U;
        }
    }

    return ret;
}

static const char *getCandidateTypeString( IceCandidateType_t candidateType )
{
    const char *ret;

    switch( candidateType )
    {
        case ICE_CANDIDATE_TYPE_HOST:
            ret = ICE_CONTROLLER_CANDIDATE_TYPE_HOST_STRING;
            break;
        case ICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
            ret = ICE_CONTROLLER_CANDIDATE_TYPE_PRFLX_STRING;
            break;
        case ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
            ret = ICE_CONTROLLER_CANDIDATE_TYPE_SRFLX_STRING;
            break;
        case ICE_CANDIDATE_TYPE_RELAYED:
            ret = ICE_CONTROLLER_CANDIDATE_TYPE_RELAY_STRING;
            break;
        default:
            ret = ICE_CONTROLLER_CANDIDATE_TYPE_UNKNOWN_STRING;
            break;
    }

    return ret;
}

static int32_t sendIceCandidateCompleteCallback( SignalingControllerEventStatus_t status, void *pUserContext )
{
    LogDebug( ( "Freeing buffer at %p", pUserContext ) );
    free( pUserContext );

    return 0;
}

static IceControllerResult_t sendIceCandidate( IceControllerContext_t *pCtx, IceCandidate_t *pCandidate, IceControllerRemoteInfo_t *pRemoteInfo )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    int written;
    char *pBuffer;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerEventMessage_t eventMessage = {
        .event = SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE,
        .onCompleteCallback = sendIceCandidateCompleteCallback,
        .pOnCompleteCallbackContext = NULL,
    };
    char pCandidateStringBuffer[ ICE_CANDIDATE_JSON_CANDIDATE_MAX_LENGTH ];

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( pCandidate->ipAddress.ipAddress.family == STUN_ADDRESS_IPv4 )
        {
            written = snprintf( pCandidateStringBuffer, ICE_CANDIDATE_JSON_CANDIDATE_MAX_LENGTH, ICE_CANDIDATE_JSON_CANDIDATE_IPV4_TEMPLATE,
                                pCtx->candidateFoundationCounter++,
                                pCandidate->priority,
                                pCandidate->ipAddress.ipAddress.address[0], pCandidate->ipAddress.ipAddress.address[1], pCandidate->ipAddress.ipAddress.address[2], pCandidate->ipAddress.ipAddress.address[3],
                                pCandidate->ipAddress.ipAddress.port,
                                getCandidateTypeString( pCandidate->iceCandidateType ) );
        }
        else
        {
            written = snprintf( pCandidateStringBuffer, ICE_CANDIDATE_JSON_CANDIDATE_MAX_LENGTH, ICE_CANDIDATE_JSON_CANDIDATE_IPV6_TEMPLATE,
                                pCtx->candidateFoundationCounter++,
                                pCandidate->priority,
                                pCandidate->ipAddress.ipAddress.address[0], pCandidate->ipAddress.ipAddress.address[1], pCandidate->ipAddress.ipAddress.address[2], pCandidate->ipAddress.ipAddress.address[3],
                                pCandidate->ipAddress.ipAddress.address[4], pCandidate->ipAddress.ipAddress.address[5], pCandidate->ipAddress.ipAddress.address[6], pCandidate->ipAddress.ipAddress.address[7],
                                pCandidate->ipAddress.ipAddress.address[8], pCandidate->ipAddress.ipAddress.address[9], pCandidate->ipAddress.ipAddress.address[10], pCandidate->ipAddress.ipAddress.address[11],
                                pCandidate->ipAddress.ipAddress.address[12], pCandidate->ipAddress.ipAddress.address[13], pCandidate->ipAddress.ipAddress.address[14], pCandidate->ipAddress.ipAddress.address[15],
                                pCandidate->ipAddress.ipAddress.port,
                                getCandidateTypeString( pCandidate->iceCandidateType ) );
        }

        if( written < 0 )
        {
            LogError( ( "snprintf returns fail, errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_CANDIDATE_STRING_BUFFER_TOO_SMALL;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Format this into candidate string. */
        pBuffer = ( char * ) malloc( ICE_CANDIDATE_JSON_MAX_LENGTH );
        LogDebug( ( "Allocating buffer at %p", pBuffer ) );
        memset( pBuffer, 0, sizeof( pBuffer ) );

        written = snprintf( pBuffer, ICE_CANDIDATE_JSON_MAX_LENGTH, ICE_CANDIDATE_JSON_TEMPLATE,
                            written, pCandidateStringBuffer );

        if( written < 0 )
        {
            LogError( ( "snprintf returns fail, errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_CANDIDATE_BUFFER_TOO_SMALL;
            free( pBuffer );
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        eventMessage.eventContent.correlationIdLength = 0U;
        eventMessage.eventContent.messageType = SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE;
        eventMessage.eventContent.pDecodeMessage = pBuffer;
        eventMessage.eventContent.decodeMessageLength = written;
        memcpy( eventMessage.eventContent.remoteClientId, pRemoteInfo->remoteClientId, pRemoteInfo->remoteClientIdLength );
        eventMessage.eventContent.remoteClientIdLength = pRemoteInfo->remoteClientIdLength;

        /* We dynamically allocate buffer for signaling controller to keep using it.
         * callback it as context to free memory. */
        eventMessage.pOnCompleteCallbackContext = pBuffer;

        signalingControllerReturn = SignalingController_SendMessage( pCtx->pSignalingControllerContext, &eventMessage );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Send signaling message fail, result: %d", signalingControllerReturn ) );
            ret = ICE_CONTROLLER_RESULT_CANDIDATE_SEND_FAIL;
            free( pBuffer );
        }
    }

    return ret;
}

static IceControllerResult_t addPollingEvent( int socketFd, struct pollfd *pFds, size_t *pFdsCount, size_t fdsCountMax, uint16_t events )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( *pFdsCount >= fdsCountMax )
    {
        LogError( ( "No enough buffer for fds" ) );
        ret = ICE_CONTROLLER_RESULT_RFDS_TOO_SMALL;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        pFds[ *pFdsCount ].fd = socketFd;
        pFds[ *pFdsCount ].events = events;
        pFds[ *pFdsCount ].revents = 0;
        (*pFdsCount)++;
    }

    return ret;
}

IceControllerResult_t IceControllerNet_AttachPolling( IceControllerContext_t *pCtx, IceControllerSocketContext_t *pSocketContext )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( pCtx == NULL || pSocketContext == NULL )
    {
        LogError( ( "Invalid input" ) );
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        ret = addPollingEvent( pSocketContext->socketFd, pCtx->fds, &pCtx->fdsCount, AWS_MAX_VIEWER_NUM * ICE_MAX_LOCAL_CANDIDATE_COUNT + 1, POLLIN );
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        pCtx->pFdsMapContext[ pCtx->fdsCount - 1 ] = pSocketContext;
    }

    return ret;
}

void IceControllerNet_DetachPolling( IceControllerContext_t *pCtx, IceControllerSocketContext_t *pSocketContext )
{
    size_t i;

    if( pSocketContext->socketFd != -1 )
    {
        for( i=0 ; i<pCtx->fdsCount ; i++ )
        {
            if( pSocketContext->socketFd == pCtx->fds[ i ].fd )
            {
                if( i != pCtx->fdsCount - 1 )
                {
                    /* If detaching handler is not the latest one, move all handlers ahead. */
                    memcpy( &pCtx->fds[ i ], &pCtx->fds[ i + 1 ], pCtx->fdsCount - 1 - i );
                    memcpy( &pCtx->pFdsMapContext[ i ], &pCtx->pFdsMapContext[ i + 1 ], pCtx->fdsCount - 1 - i );
                }

                /* Reset latest polling FD, counter, and mapped context. */
                memset( &pCtx->fds[ pCtx->fdsCount - 1 ], 0, sizeof( struct pollfd ) );
                pCtx->pFdsMapContext[ pCtx->fdsCount - 1 ] = NULL;
                pCtx->fdsCount--;
            }
        }
    }
}

void IceControllerNet_FreeSocketContext( IceControllerContext_t *pCtx, IceControllerSocketContext_t *pSocketContext )
{
    if( pSocketContext->socketFd != -1 )
    {
        IceControllerNet_DetachPolling( pCtx, pSocketContext );

        close( pSocketContext->socketFd );
        pSocketContext->socketFd = -1;
    }
}

static IceControllerResult_t IceControllerNet_AddPollingEvent( int socketFd, struct pollfd *pFds, size_t *pFdsCount, size_t fdsCountMax )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( *pFdsCount >= fdsCountMax )
    {
        LogError( ( "No enough buffer for fds" ) );
        ret = ICE_CONTROLLER_RESULT_RFDS_TOO_SMALL;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        pFds[ *pFdsCount ].fd = socketFd;
        pFds[ *pFdsCount ].events = POLLIN;
        pFds[ *pFdsCount ].revents = 0;
        *pFdsCount++;
    }

    return ret;
}

IceControllerResult_t IceControllerNet_ConvertIpString( const char *pIpAddr, size_t ipAddrLength, IceIPAddress_t *pDest )
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

        if( inet_pton( AF_INET, ipAddress, pDest->ipAddress.address ) == 1 )
        {
            pDest->ipAddress.family = STUN_ADDRESS_IPv4;
        }
        else if( inet_pton( AF_INET6, ipAddress, pDest->ipAddress.address ) == 1 )
        {
            pDest->ipAddress.family = STUN_ADDRESS_IPv6;
        }
        else
        {
            ret = ICE_CONTROLLER_RESULT_INVALID_IP_ADDR;
        }
    }

    return ret;
}

IceControllerResult_t IceControllerNet_Htons( uint16_t port, uint16_t *pOutPort )
{
    *pOutPort = htons( port );

    return ICE_CONTROLLER_RESULT_OK;
}

IceControllerResult_t IceControllerNet_InitRemoteInfo( IceControllerRemoteInfo_t *pRemoteInfo )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    uint32_t i;

    for( i=0 ; i<ICE_MAX_CANDIDATE_PAIR_COUNT ; i++ )
    {
        /* Initialize all socket fd to -1. */
        pRemoteInfo->socketsContexts[i].socketFd = -1;
    }

    return ret;
}

IceControllerResult_t IceControllerNet_SendPacket( IceControllerSocketContext_t *pSocketContext, IceIPAddress_t *pDestinationIpAddress, char *pBuffer, size_t length )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    int sentBytes, sendTotalBytes=0;
    struct sockaddr* pDestinationAddress = NULL;
    struct sockaddr_in ipv4Address;
    struct sockaddr_in6 ipv6Address;
    socklen_t addressLength = 0;

    /* Set socket destination address, including IP type (v4/v6), IP address and port. */
    if( pDestinationIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 )
    {
        memset( &ipv4Address, 0, sizeof(ipv4Address) );
        ipv4Address.sin_family = AF_INET;
        ipv4Address.sin_port = htons( pDestinationIpAddress->ipAddress.port );
        memcpy( &ipv4Address.sin_addr, pDestinationIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE );

        pDestinationAddress = (struct sockaddr*) &ipv4Address;
        addressLength = sizeof( ipv4Address );
    }
    else
    {
        memset( &ipv6Address, 0, sizeof(ipv6Address) );
        ipv6Address.sin6_family = AF_INET6;
        ipv6Address.sin6_port = htons( pDestinationIpAddress->ipAddress.port );
        memcpy( &ipv6Address.sin6_addr, pDestinationIpAddress->ipAddress.address, STUN_IPV6_ADDRESS_SIZE );

        pDestinationAddress = (struct sockaddr*) &ipv6Address;
        addressLength = sizeof( ipv6Address );
    }

    /* Send data */
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
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                /* Just retry for these kinds of errno. */
            }
            else
            {
                LogWarn( ( "Send error, errno: %s", strerror( errno ) ) );
                ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_SENDTO;
                break;
            }
        }
        else
        {
            sendTotalBytes += sentBytes;
        }
    }

    return ret;
}

void IceControllerNet_AddSrflxaCndidate( IceControllerContext_t *pCtx, IceControllerRemoteInfo_t *pRemoteInfo, IceIPAddress_t *pLocalIpAddress )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceResult_t iceResult;
    uint32_t i;
    IceCandidate_t *pCandidate;
    IceControllerSocketContext_t *pSocketContext;
    /* TODO: not necessary to prepare STUN buffer after fix. */
    uint8_t *pStunBuffer;
    uint32_t stunBufferLength;
    char transactionIdBuffer[ STUN_HEADER_TRANSACTION_ID_LENGTH ];

    for( i=0 ; i<pCtx->iceServersCount ; i++ )
    {
        /* Reset ret for every round. */
        ret = ICE_CONTROLLER_RESULT_OK;

        if( pCtx->iceServers[ i ].serverType != ICE_CONTROLLER_ICE_SERVER_TYPE_STUN )
        {
            /* Not STUN server, no need to create srflx candidate for this server. */
            continue;
        }
        else if( pCtx->iceServers[ i ].ipAddress.ipAddress.family != STUN_ADDRESS_IPv4 )
        {
            /* For srflx candidate, we only support IPv4 for now. */
            continue;
        }
        else
        {
            /* Do nothing, coverity happy. */
        }

        /* Only support IPv4 STUN for now. */
        if( pCtx->iceServers[ i ].ipAddress.ipAddress.family == STUN_ADDRESS_IPv4 )
        {
            pSocketContext = &pRemoteInfo->socketsContexts[ pRemoteInfo->socketsContextsCount ];
            LogDebug( ( "Create srflx candidate with local IP --" ) );
            IceControllerNet_LogIpAddressInfo( pLocalIpAddress );
            ret = createSocketConnection( &pSocketContext->socketFd, pLocalIpAddress, ICE_SOCKET_PROTOCOL_UDP );
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            iceResult = Ice_AddSrflxCandidate( *pLocalIpAddress, &pRemoteInfo->iceAgent, &pCandidate,
                                               transactionIdBuffer, &pStunBuffer, &stunBufferLength );
            if( iceResult != ICE_RESULT_OK )
            {
                /* Free resource that already created. */
                LogError( ( "Ice_AddSrflxCandidate fail, result: %d", iceResult ) );
                IceControllerNet_FreeSocketContext( pCtx, pSocketContext );
                ret = ICE_CONTROLLER_RESULT_FAIL_ADD_HOST_CANDIDATE;
                break;
            }
        }

        /* TODO: Send STUN request to STUN server to get IP address as srflx candidate. */
        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            ret = IceControllerNet_SendPacket( pSocketContext, &pCtx->iceServers[ i ].ipAddress, pStunBuffer, stunBufferLength );
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            ret = IceControllerNet_AttachPolling( pCtx, pSocketContext );
            if( ret != ICE_CONTROLLER_RESULT_OK )
            {
                /* Free resource that already created. */
                IceControllerNet_FreeSocketContext( pCtx, pSocketContext );
                break;
            }
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            pSocketContext->pLocalCandidate = pCandidate;
            pSocketContext->candidateType = ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
            pSocketContext->pRemoteInfo = pRemoteInfo;
            pRemoteInfo->socketsContextsCount++;
        }
    }
}

IceControllerResult_t IceControllerNet_AddLocalCandidates( IceControllerContext_t *pCtx, IceControllerRemoteInfo_t *pRemoteInfo )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceResult_t iceResult;
    uint32_t i;
    IceCandidate_t *pCandidate;
    IceControllerSocketContext_t *pSocketContext;

    pCtx->localIpAddressesCount = ICE_MAX_LOCAL_CANDIDATE_COUNT;
    getLocalIPAdresses( pCtx->localIpAddresses, &pCtx->localIpAddressesCount );

    for( i=0 ; i<pCtx->localIpAddressesCount ; i++ )
    {
        pSocketContext = &pRemoteInfo->socketsContexts[ pRemoteInfo->socketsContextsCount ];
        ret = createSocketConnection( &pSocketContext->socketFd, &pCtx->localIpAddresses[i], ICE_SOCKET_PROTOCOL_UDP );

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            iceResult = Ice_AddHostCandidate( pCtx->localIpAddresses[i], &pRemoteInfo->iceAgent, &pCandidate );
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
            ret = IceControllerNet_AttachPolling( pCtx, pSocketContext );
            if( ret != ICE_CONTROLLER_RESULT_OK )
            {
                /* Free resource that already created. */
                IceControllerNet_FreeSocketContext( pCtx, pSocketContext );
                break;
            }
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            ret = sendIceCandidate( pCtx, pCandidate, pRemoteInfo );
            if( ret != ICE_CONTROLLER_RESULT_OK )
            {
                /* Free resource that already created. */
                IceControllerNet_FreeSocketContext( pCtx, pSocketContext );
                break;
            }
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            pSocketContext->pLocalCandidate = pCandidate;
            pSocketContext->candidateType = ICE_CANDIDATE_TYPE_HOST;
            pSocketContext->pRemoteInfo = pRemoteInfo;
            pRemoteInfo->socketsContextsCount++;
            
            LogDebug( ( "Created host candidate with IP address -- " ) );
            IceControllerNet_LogIpAddressInfo( &pCtx->localIpAddresses[i] );
        }

        /* Prepare srflx candidates based on current host candidate. */
        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            IceControllerNet_AddSrflxaCndidate( pCtx, pRemoteInfo, &pCtx->localIpAddresses[i] );
        }
    }

    return ret;
}

IceControllerResult_t IceControllerNet_HandleRxPacket( IceControllerContext_t *pCtx, IceControllerSocketContext_t *pSocketContext )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceResult_t iceResult;
    int readBytes;
    struct sockaddr srcAddress;
    socklen_t srcAddressLength = sizeof( srcAddress );
    char transactionIdBuffer[ STUN_HEADER_TRANSACTION_ID_LENGTH ];
    struct sockaddr_in* pIpv4Address;
    struct sockaddr_in6* pIpv6Address;
    IceIPAddress_t remoteAddress;
    /* TODO: workaround here, we should be able to remove candidatePair after fixing Ice_HandleStunPacket. */
    IceCandidatePair_t *pCandidatePair;
    uint8_t *pSentStunBuffer;
    uint32_t sentStunBufferLength;
    uint8_t skipProcess = 0;

    readBytes = recvfrom( pSocketContext->socketFd, receiveBuffer, RX_BUFFER_SIZE, 0, &srcAddress, &srcAddressLength );
    if( readBytes < 0 )
    {
        LogError( ( "Fail to receive packets from socket ID: %d, errno: %s", pSocketContext->socketFd, strerror( errno ) ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_RECVFROM;
    }
    else if( readBytes == 0 )
    {
        /* Nothing to do if receive 0 byte. */
        LogDebug( ( "Have RX event but receive no data." ) );
    }
    else
    {
        /* Received data, handle this STUN message. */
        if( srcAddress.sa_family == AF_INET )
        {
            pIpv4Address = (struct sockaddr_in*) &srcAddress;

            remoteAddress.ipAddress.family = STUN_ADDRESS_IPv4;
            remoteAddress.ipAddress.port = ntohs( pIpv4Address->sin_port );
            memcpy( remoteAddress.ipAddress.address, &pIpv4Address->sin_addr, STUN_IPV4_ADDRESS_SIZE );
        }
        else if( srcAddress.sa_family == AF_INET6 )
        {
            pIpv6Address = (struct sockaddr_in6*) &srcAddress;

            remoteAddress.ipAddress.family = STUN_ADDRESS_IPv6;
            remoteAddress.ipAddress.port = ntohs( pIpv6Address->sin6_port );
            memcpy( remoteAddress.ipAddress.address, &pIpv6Address->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
        }
        else
        {
            /* Unknown IP type, drop packet. */
            ret = ICE_CONTROLLER_RESULT_INVALID_RX_PACKET_FAMILY;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        LogDebug( ( "Receiving %d bytes from IP --", readBytes ) );
        IceControllerNet_LogIpAddressInfo( &remoteAddress );
        
        LogDebug( ( "Receiving STUN packets: STUN header:\n"
                    "type: 0x%02x%02x\n"
                    "length:: 0x%02x%02x\n"
                    "transaction ID: 0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
                    *(uint8_t*)(receiveBuffer), *(uint8_t*)(receiveBuffer + 1),
                    *(uint8_t*)(receiveBuffer + 2), *(uint8_t*)(receiveBuffer + 3),
                    *(uint8_t*)(receiveBuffer + 8), *(uint8_t*)(receiveBuffer + 9), *(uint8_t*)(receiveBuffer + 10), *(uint8_t*)(receiveBuffer + 11),
                    *(uint8_t*)(receiveBuffer + 12), *(uint8_t*)(receiveBuffer + 13), *(uint8_t*)(receiveBuffer + 14), *(uint8_t*)(receiveBuffer + 15),
                    *(uint8_t*)(receiveBuffer + 16), *(uint8_t*)(receiveBuffer + 17), *(uint8_t*)(receiveBuffer + 18), *(uint8_t*)(receiveBuffer + 19) ) );
        
        for( int i=0; i<readBytes; i+=4 )
        {
            LogDebug( ("%02x %02x %02x %02x", receiveBuffer[ i ], receiveBuffer[ i+1 ], receiveBuffer[ i+2 ], receiveBuffer[ i+3 ]) );
        }

        iceResult = Ice_HandleStunPacket( &pSocketContext->pRemoteInfo->iceAgent,
                                          receiveBuffer,
                                          ( uint32_t ) readBytes,
                                          transactionIdBuffer,
                                          &pSentStunBuffer,
                                          &sentStunBufferLength,
                                          &pSocketContext->pLocalCandidate->ipAddress,
                                          &remoteAddress,
                                          &pCandidatePair );
        
        switch( iceResult )
        {
            case ICE_RESULT_UPDATED_SRFLX_CANDIDATE_ADDRESS:
                /* This packet is actually from STUN server for SRFLX candidate. Send srflx ICE candidate to remote peer. */
                if( sendIceCandidate( pCtx, pSocketContext->pLocalCandidate, pSocketContext->pRemoteInfo ) != ICE_CONTROLLER_RESULT_OK )
                {
                    /* Just ignore this failing case and continue the ICE procedure. */
                    LogWarn( ( "Fail to send server reflexive candidate to remote peer, result" ) );
                }
                break;
            case ICE_RESULT_SEND_RESPONSE_FOR_NOMINATION:
                if( Ice_CreateResponseForRequest( &pSocketContext->pRemoteInfo->iceAgent,
                                                  &pSentStunBuffer,
                                                  &sentStunBufferLength,
                                                  &pCandidatePair->pRemote->ipAddress,
                                                  transactionIdBuffer ) != ICE_RESULT_OK )
                {
                    LogWarn( ( "Unable to create STUN response for nomination" ) );
                }
                else
                {
                    if( IceControllerNet_SendPacket( pSocketContext, &pCandidatePair->pRemote->ipAddress, pSentStunBuffer, sentStunBufferLength ) != ICE_CONTROLLER_RESULT_OK )
                    {
                        LogWarn( ( "Unable to send STUN response for nomination" ) );
                    }
                }
                break;
            case ICE_RESULT_SEND_TRIGGERED_CHECK:
                if( Ice_CreateResponseForRequest( &pSocketContext->pRemoteInfo->iceAgent,
                                                  &pSentStunBuffer,
                                                  &sentStunBufferLength,
                                                  &pCandidatePair->pRemote->ipAddress,
                                                  transactionIdBuffer ) != ICE_RESULT_OK )
                {
                    LogWarn( ( "Unable to create response for triggered check" ) );
                    skipProcess = 1;
                }

                if( !skipProcess )
                {
                    if( IceControllerNet_SendPacket( pSocketContext, &pCandidatePair->pRemote->ipAddress, pSentStunBuffer, sentStunBufferLength ) != ICE_CONTROLLER_RESULT_OK )
                    {
                        LogWarn( ( "Unable to send STUN response for triggered check" ) );
                        skipProcess = 1;
                    }
                }

                if( !skipProcess )
                {
                    if( Ice_CreateRequestForConnectivityCheck( &pSocketContext->pRemoteInfo->iceAgent,
                                                               &pSentStunBuffer,
                                                               &sentStunBufferLength,
                                                               transactionIdBuffer,
                                                               pCandidatePair ) != ICE_CONTROLLER_RESULT_OK )
                    {
                        LogWarn( ( "Unable to create STUN request for triggered check" ) );
                        skipProcess = 1;
                    }
                }

                if( !skipProcess )
                {
                    if( IceControllerNet_SendPacket( pSocketContext, &pCandidatePair->pRemote->ipAddress, pSentStunBuffer, sentStunBufferLength ) != ICE_CONTROLLER_RESULT_OK )
                    {
                        LogWarn( ( "Unable to send STUN request for triggered check" ) );
                        skipProcess = 1;
                    }
                }
                break;
            case ICE_RESULT_SEND_RESPONSE_FOR_REMOTE_REQUEST:
                if( Ice_CreateResponseForRequest( &pSocketContext->pRemoteInfo->iceAgent,
                                                  &pSentStunBuffer,
                                                  &sentStunBufferLength,
                                                  &pCandidatePair->pRemote->ipAddress,
                                                  transactionIdBuffer ) != ICE_RESULT_OK )
                {
                    LogWarn( ( "Unable to create response for remote request" ) );
                    skipProcess = 1;
                }

                if( !skipProcess )
                {
                    if( IceControllerNet_SendPacket( pSocketContext, &pCandidatePair->pRemote->ipAddress, pSentStunBuffer, sentStunBufferLength ) != ICE_CONTROLLER_RESULT_OK )
                    {
                        LogWarn( ( "Unable to send STUN response for remote request" ) );
                        skipProcess = 1;
                    }
                }
                break;
            case ICE_RESULT_START_NOMINATION:
                LogWarn( ( "ICE_RESULT_START_NOMINATION" ) );
                break;
            case ICE_RESULT_CANDIDATE_PAIR_READY:
                LogWarn( ( "ICE_RESULT_CANDIDATE_PAIR_READY" ) );
                break;
            default:
                LogWarn( ( "Unknown case: %d", iceResult ) );
                break;
        }
    }


    return ret;
}

IceControllerResult_t IceControllerNet_DnsLookUp( char *pUrl, StunAttributeAddress_t *pIpAddress )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    int dnsResult;
    struct addrinfo *pResult = NULL;
    struct addrinfo *pIterator;
    struct sockaddr_in* ipv4Address;
    struct sockaddr_in6* ipv6Address;

    if( pUrl == NULL || pIpAddress == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        dnsResult = getaddrinfo( pUrl, NULL, NULL, &pResult );
        if( dnsResult != 0 )
        {
            LogWarn( ( "DNS query failing, url: %s, result: %s", pUrl, dnsResult == EAI_SYSTEM? strerror( errno ) : gai_strerror( dnsResult ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_DNS_QUERY;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        for( pIterator = pResult ; pIterator ; pIterator = pIterator->ai_next )
        {
            if( pIterator->ai_family == AF_INET )
            {
                ipv4Address = (struct sockaddr_in*) pIterator->ai_addr;
                pIpAddress->family = STUN_ADDRESS_IPv4;
                memcpy( pIpAddress->address, &ipv4Address->sin_addr, STUN_IPV4_ADDRESS_SIZE );
                break;
            }
            else if( pIterator->ai_family == AF_INET6 )
            {
                ipv6Address = (struct sockaddr_in6*) pIterator->ai_addr;
                pIpAddress->family = STUN_ADDRESS_IPv6;
                memcpy( pIpAddress->address, &ipv6Address->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
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

void IceControllerNet_LogIpAddressInfo( IceIPAddress_t *pIceIpAddress )
{
    char ipBuffer[ INET_ADDRSTRLEN ];
    LogDebug( ( "IP address/port: %s/%d", inet_ntop( AF_INET, pIceIpAddress->ipAddress.address, ipBuffer, INET_ADDRSTRLEN ), pIceIpAddress->ipAddress.port ) );
}
