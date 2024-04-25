#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <poll.h>
#include <errno.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
#include "ice_api.h"
#include "stun_endianness.h"
#include "core_json.h"
#include "string_utils.h"

#define ICE_CONTROLLER_CANDIDATE_JSON_KEY "candidate"

/* Generate a printable string that does not
 * need to be escaped when encoding in JSON
 */
static void generateJSONValidString( char *pDst, size_t length )
{
    size_t i = 0;
    uint8_t skipProcess = 0;
    const char jsonCharSet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
    const size_t jsonCharSetLength = strlen( jsonCharSet );

    if( pDst == NULL )
    {
        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        for( i = 0; i < length; i++ )
        {
            pDst[i] = jsonCharSet[ rand() % jsonCharSetLength ];
        }
    }
}

static IceControllerResult_t parseIceCandidate( const char *pDecodeMessage, size_t decodeMessageLength, const char **ppCandidateString, size_t *pCandidateStringLength )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    JSONStatus_t jsonResult;
    size_t start = 0, next = 0;
    JSONPair_t pair = { 0 };
    uint8_t isCandidateFound = 0;

    jsonResult = JSON_Validate( pDecodeMessage, decodeMessageLength );
    if( jsonResult != JSONSuccess)
    {
        ret = ICE_CONTROLLER_RESULT_INVALID_JSON;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Check if it's SDP offer. */
        jsonResult = JSON_Iterate( pDecodeMessage, decodeMessageLength, &start, &next, &pair );

        while( jsonResult == JSONSuccess )
        {
            if( pair.keyLength == strlen( ICE_CONTROLLER_CANDIDATE_JSON_KEY ) &&
                strncmp( pair.key, ICE_CONTROLLER_CANDIDATE_JSON_KEY, pair.keyLength ) == 0 )
            {
                *ppCandidateString = pair.value;
                *pCandidateStringLength = pair.valueLength;
                isCandidateFound = 1;

                break;
            }

            jsonResult = JSON_Iterate( pDecodeMessage, decodeMessageLength, &start, &next, &pair );
        }
    }

    if( isCandidateFound == 0 )
    {
        ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_NOT_FOUND;
    }

    return ret;
}

// static void*
// listenForIncomingPackets( void *d )
// {
//     int retval = 0;
//     while( retval <= 0 )
//     {
//         retval = poll(rfds, nfds, CONNECTION_LISTENER_SOCKET_WAIT_FOR_DATA_TIMEOUT / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
//     }
// }

// int socketBind( IceIPAddress_t * pHostIpAddress, int sockfd )
// {
//     int retStatus = 0;
//     struct sockaddr_in ipv4Addr;
//     struct sockaddr_in6 ipv6Addr;
//     struct sockaddr* sockAddr = NULL;
//     socklen_t addrLen;

//     char ipAddrStr[KVS_IP_ADDRESS_STRING_BUFFER_LEN];

//     if ( pHostIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 ) {
//         memset(&ipv4Addr, 0x00, sizeof(ipv4Addr));
//         ipv4Addr.sin_family = AF_INET;
//         ipv4Addr.sin_port = 0; // use next available port
//         memcpy(&ipv4Addr.sin_addr, pHostIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE);
//         sockAddr = (struct sockaddr*) &ipv4Addr;
//         addrLen = sizeof(struct sockaddr_in);

//     } else {
//         memset(&ipv6Addr, 0x00, sizeof(ipv6Addr));
//         ipv6Addr.sin6_family = AF_INET6;
//         ipv6Addr.sin6_port = 0; // use next available port
//         memcpy(&ipv6Addr.sin6_addr, pHostIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE);
//         sockAddr = (struct sockaddr*) &ipv6Addr;
//         addrLen = sizeof(struct sockaddr_in6);
//     }

//     if (bind(sockfd, sockAddr, addrLen) < 0) {
//         printf("bind() failed for ip%s address: %s, port %u with errno %s", IS_IPV4_ADDR(pHostIpAddress->ipAddress) ? "" : "V6", ipAddrStr,
//               pHostIpAddress->ipAddress.port, strerror(errno));
//     }

//     if (getsockname(sockfd, sockAddr, &addrLen) < 0) {
//         printf("getsockname() failed with errno %s", strerror(errno));

//     }

//     pHostIpAddress->ipAddress.port = (uint16_t) pHostIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 ? ipv4Addr.sin_port : ipv6Addr.sin6_port;

//     return retStatus;
// }

// int createSocketConnection( IceIPAddress_t * pBindAddr )
// {
//     int retStatus;

//     sockets[ socketUsed ] = socket( pBindAddr->ipAddress.family == STUN_ADDRESS_IPv4 ? AF_INET : AF_INET6, SOCK_DGRAM, 0 );

//     if ( sockets[ socketUsed ] == -1) 
//     {
//         printf("socket() failed to create socket with errno %s", strerror(errno));
//     }
    
//     if( pBindAddr )
//     {
//         retStatus = socketBind( pBindAddr, sockets[ socketUsed ] );
//     }
//     if( !retStatus )
//     {
//         socketUsed++;
//         rfds[nfds].fd = sockets[ socketUsed - 1 ];
//         rfds[nfds].events = POLLIN | POLLPRI;
//         rfds[nfds].revents = 0;
//         nfds++;
//     }
//     return retStatus;
// }

// void getLocalIPAdresses( void )
// {
//     struct ifaddrs *ifap, *ifa;
//     struct sockaddr_in *sa;
//     struct sockaddr_in6 *saIPv6;
//     struct sockaddr_in* pIpv4Addr = NULL;
//     struct sockaddr_in6* pIpv6Addr = NULL;
//     char addr[ 16 ];

//     getifaddrs (&ifap);
//     for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
//         if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET ) 
//         {
//             destIp[ destIpLen ].ipAddress.family = STUN_ADDRESS_IPv4;
//             destIp[ destIpLen ].ipAddress.port = 0;
//             pIpv4Addr = (struct sockaddr_in*) ifa->ifa_addr;
//             memcpy( destIp[ destIpLen ].ipAddress.address , &pIpv4Addr->sin_addr, STUN_IPV4_ADDRESS_SIZE );
//             destIp[ destIpLen ].isPointToPoint = ((ifa->ifa_flags & IFF_POINTOPOINT) != 0);
//             destIpLen++;
//         }
//         else if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET6)
//         {
//             getnameinfo( ifa->ifa_addr, sizeof(struct sockaddr_in6), addr, sizeof(addr), NULL, 0, NI_NUMERICHOST );
//             destIp[ destIpLen ].ipAddress.family = STUN_ADDRESS_IPv6;
//             destIp[ destIpLen ].ipAddress.port = 0;
//             pIpv6Addr = (struct sockaddr_in6*) ifa->ifa_addr;
//             memcpy( destIp[ destIpLen ].ipAddress.address , &pIpv6Addr->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
//             destIp[ destIpLen ].isPointToPoint = ((ifa->ifa_flags & IFF_POINTOPOINT) != 0);
//             destIpLen++;
//         }
//     }

//     freeifaddrs(ifap);
//     return ;
// }

// int IceController_AddHostCandidates( IceAgent_t * pIceAgent )
// {
//     int retStatus;
//     int i;
//     getLocalIPAdresses();

//     for( i = 0; i < destIpLen-1; i++ )
//     {
//         retStatus = createSocketConnection( &destIp[ i ] );

//         if( retStatus == 0 )
//         {
            
//             retStatus = Ice_AddHostCandidate( destIp[ i ], pIceAgent );
//             if( retStatus != 0 )
//             {
//                 printf(" Host Candidate addition failed\n ");
//             }
//         }
//         else
//         {
//             printf("Socket creation failed\n");
//         }
//     }

// }

// int IceController_Init( char * localName, char * remoteName, char * localPwd, char * remotePwd )
IceControllerResult_t IceController_Init( IceControllerContext_t *pCtx )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( pCtx == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        memset( pCtx, 0, sizeof( IceControllerContext_t ) );

        /* Generate local name/password. */
        generateJSONValidString( pCtx->localUserName, ICE_CONTROLLER_USER_NAME_LENGTH );
        pCtx->localUserName[ ICE_CONTROLLER_USER_NAME_LENGTH ] = '\0';
        generateJSONValidString( pCtx->localPassword, ICE_CONTROLLER_PASSWORD_LENGTH );
        pCtx->localPassword[ ICE_CONTROLLER_PASSWORD_LENGTH ] = '\0';
    }

    return ret;
}

IceControllerResult_t IceController_DeserializeIceCandidate( const char *pDecodeMessage, size_t decodeMessageLength, IceControllerCandidate_t *pCandidate )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    StringUtilsResult_t stringResult;
    const char *pCandidateString;
    size_t candidateStringLength;
    const char *pCurr, *pTail, *pNext;
    size_t tokenLength;
    IceControllerCandidateDeserializerState_t deserializerState = ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_FOUNDATION;
    uint8_t isAllElementsParsed = 0;

    if( pDecodeMessage == NULL || pCandidate == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* parse json message and get the candidate string. */
        ret = parseIceCandidate( pDecodeMessage, decodeMessageLength, &pCandidateString, &candidateStringLength );

        pCurr = pCandidateString;
        pTail = pCandidateString + candidateStringLength;
    }

    /* deserialize candidate string into structure. */
    while( ret == ICE_CONTROLLER_RESULT_OK &&
           ( pNext = memchr( pCurr, ' ', pTail - pCurr ) ) != NULL &&
           deserializerState <= ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_MAX )
    {
        tokenLength = pNext - pCurr;

        switch( deserializerState )
        {
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_FOUNDATION:
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_COMPONENT:
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PROTOCOL:
                if( strncmp( pCurr, "tcp", tokenLength ) == 0 )
                {
                    pCandidate->protocol = ICE_SOCKET_PROTOCOL_TCP;
                }
                else if( strncmp( pCurr, "udp", tokenLength ) == 0 )
                {
                    pCandidate->protocol = ICE_SOCKET_PROTOCOL_UDP;
                }
                else
                {
                    LogWarn( ( "unknown protocol %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PROTOCOL;
                }
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PRIORITY:
                stringResult = StringUtils_ConvertStringToUl( pCurr, tokenLength, &pCandidate->priority );
                if( stringResult != STRING_UTILS_RESULT_OK )
                {
                    LogWarn( ( "Invalid priority %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PRIORITY;
                }
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_IP:
                ret = IceControllerNet_ConvertIpString( pCurr, tokenLength, &pCandidate->iceIpAddress );
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PORT:
                stringResult = StringUtils_ConvertStringToUl( pCurr, tokenLength, (uint32_t *) &pCandidate->port );
                if( stringResult != STRING_UTILS_RESULT_OK )
                {
                    LogWarn( ( "Invalid port %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PORT;
                }
                else
                {
                    /* Also update port in address following network endianness. */
                    ret = IceControllerNet_Htos( pCandidate->port, &pCandidate->iceIpAddress.ipAddress.port );
                }
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_TYPE_ID:
                if( tokenLength != strlen( "typ" ) || strncmp( pCurr, "typ", tokenLength ) != 0 )
                {
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_TYPE_ID;
                }
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_TYPE_VAL:
                isAllElementsParsed = 1;

                if( strncmp( pCurr, "host", tokenLength ) == 0 )
                {
                    pCandidate->candidateType = ICE_CANDIDATE_TYPE_HOST;
                }
                else if( strncmp( pCurr, "srflx", tokenLength ) == 0 )
                {
                    pCandidate->candidateType = ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
                }
                else if( strncmp( pCurr, "prflx", tokenLength ) == 0 )
                {
                    pCandidate->candidateType = ICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
                }
                else if( strncmp( pCurr, "relay", tokenLength ) == 0 )
                {
                    pCandidate->candidateType = ICE_CANDIDATE_TYPE_RELAYED;
                }
                else
                {
                    LogWarn( ( "unknown candidate type %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_TYPE;
                }
                break;
            default:
                break;
        }

        pCurr = pNext + 1;
        deserializerState++;
    }

    if( isAllElementsParsed != 1 )
    {
        ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_LACK_OF_ELEMENT;
    }

    return ret;
}



    // char combinedName[ MAX_ICE_CONFIG_USER_NAME_LEN ];
    // int i;

    // strcat( combinedName, localName );
    // strcat( combinedName, remoteName );

    // retStatus = Ice_CreateIceAgent( &IceAgent, localName, localPwd, remoteName, remotePwd, combinedName, &TransactionIdStore );

    // if( retStatus == 0 )
    // {
    //     for( i = 0; i < ICE_MAX_LOCAL_CANDIDATE_COUNT; i++ )
    //     {
    //         IceAgent.localCandidates[i] = &localCandidates[i];
    //     }
    //     for( i = 0; i < ICE_MAX_REMOTE_CANDIDATE_COUNT; i++ )
    //     {
    //         IceAgent.remoteCandidates[i] = &remoteCandidates[i];
    //     }
    //     for( i = 0; i < ICE_MAX_CANDIDATE_PAIR_COUNT; i++ )
    //     {
    //         IceAgent.iceCandidatePairs[i] = &candidatePairs[i];
    //     }
    //     for( i = 0; i < ICE_MAX_CANDIDATE_PAIR_COUNT; i++ )
    //     {
    //         IceAgent.stunMessageBuffers[i] = &stunBuffers[i];
    //     }

    //     retStatus = IceController_AddHostCandidates( &IceAgent );

    //     pthread_create( &thread_id, NULL, listenForIncomingPackets, &thread_id );
    // }
    // else
    // {
    //     printf("Creation of Ice Agent failed.\n");
    // }

    // return retStatus;
