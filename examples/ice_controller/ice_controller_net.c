#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <errno.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
#include "ice_api.h"
#include "signaling_controller.h"

// static void*
// listenForIncomingPackets( void *d )
// {
//     int retval = 0;
//     while( retval <= 0 )
//     {
//         retval = poll(rfds, nfds, CONNECTION_LISTENER_SOCKET_WAIT_FOR_DATA_TIMEOUT / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
//     }
// }

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
    struct sockaddr_in ipv4Addr;
    // struct sockaddr_in6 ipv6Addr;
    struct sockaddr* sockAddr = NULL;
    socklen_t addrLength;

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
            memset( &ipv4Addr, 0x00, sizeof(ipv4Addr) );
            ipv4Addr.sin_family = AF_INET;
            ipv4Addr.sin_port = 0; // use next available port
            memcpy( &ipv4Addr.sin_addr, pIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE );
            sockAddr = (struct sockaddr*) &ipv4Addr;
            addrLength = sizeof(struct sockaddr_in);
        }
        else
        {
            /* TODO: skip IPv6 for now. */
            // memset( &ipv6Addr, 0x00, sizeof(ipv6Addr) );
            // ipv6Addr.sin6_family = AF_INET6;
            // ipv6Addr.sin6_port = 0; // use next available port
            // memcpy(&ipv6Addr.sin6_addr, pHostIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE);
            // sockAddr = (struct sockaddr*) &ipv6Addr;
            // addrLength = sizeof(struct sockaddr_in6);
            ret = ICE_CONTROLLER_RESULT_IPV6_NOT_SUPPORT;
            close( *pSocketFd );
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( bind( *pSocketFd, sockAddr, addrLength ) < 0 )
        {
            LogError( ( "socket() failed to bind socket with errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_BIND;
            close( *pSocketFd );
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( getsockname( *pSocketFd, sockAddr, &addrLength ) < 0 )
        {
            LogError( ( "getsockname() failed with errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_SOCKET_GETSOCKNAME;
            close( *pSocketFd );
        }
        else
        {
            pIpAddress->ipAddress.port = ( uint16_t ) pIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 ? ipv4Addr.sin_port : 0U;
        }
    }

    return ret;
}

static IceControllerResult_t attachPolling( int socketFd, struct pollfd *pRfds, size_t *pRfdsCount )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( *pRfdsCount >= ICE_MAX_LOCAL_CANDIDATE_COUNT )
    {
        LogError( ( "No enough buffer for rfds" ) );
        ret = ICE_CONTROLLER_RESULT_RFDS_TOO_SMALL;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        pRfds[ *pRfdsCount ].fd = socketFd;
        pRfds[ *pRfdsCount ].events = POLLIN;
        pRfds[ *pRfdsCount ].revents = 0;
        *pRfdsCount++;
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

int32_t sendIceCandidateCompleteCallback( SignalingControllerEventStatus_t status, void *pUserContext )
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
                                ntohs( pCandidate->ipAddress.ipAddress.port ),
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
                                ntohs( pCandidate->ipAddress.ipAddress.port ),
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

IceControllerResult_t IceControllerNet_AddHostCandidates( IceControllerContext_t *pCtx, IceControllerRemoteInfo_t *pRemoteInfo )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceResult_t iceResult;
    uint32_t i;
    size_t localIpStartIndex = pCtx->socketsFdLocalCandidatesCount;
    size_t localIpEndIndex;
    IceCandidate_t *pCandidate;

    pCtx->localIpAddressesCount = ICE_MAX_LOCAL_CANDIDATE_COUNT;
    getLocalIPAdresses( pCtx->localIpAddresses, &pCtx->localIpAddressesCount );
    localIpEndIndex = localIpStartIndex + pCtx->localIpAddressesCount;

    for( i=localIpStartIndex ; i<localIpEndIndex ; i++ )
    {
        ret = createSocketConnection( &pCtx->socketsFdLocalCandidates[i], &pCtx->localIpAddresses[i], ICE_SOCKET_PROTOCOL_UDP );

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            iceResult = Ice_AddHostCandidate( pCtx->localIpAddresses[i], &pRemoteInfo->iceAgent, &pCandidate );
            if( iceResult != ICE_RESULT_OK )
            {
                LogError( ( "Ice_AddHostCandidate fail, result: %d", iceResult ) );
                ret = ICE_CONTROLLER_RESULT_FAIL_ADD_HOST_CANDIDATE;
                break;
            }
        }
        
        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            ret = attachPolling( pCtx->socketsFdLocalCandidates[i], pCtx->rfds, &pCtx->rfdsCount );
            if( ret != ICE_CONTROLLER_RESULT_OK )
            {
                /* Free resource that already created. */
                close( pCtx->socketsFdLocalCandidates[i] );
                break;
            }
        }

        if( ret == ICE_CONTROLLER_RESULT_OK )
        {
            ret = sendIceCandidate( pCtx, pCandidate, pRemoteInfo );
            if( ret != ICE_CONTROLLER_RESULT_OK )
            {
                /* Free resource that already created. */
                close( pCtx->socketsFdLocalCandidates[i] );
                break;
            }

            pCtx->socketsFdLocalCandidatesCount++;
        }
    }

    return ret;
}
