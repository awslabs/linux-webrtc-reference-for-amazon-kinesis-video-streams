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
#include <pthread.h>
#include "ice_controller.h"
#include "ice_api.h"
#include "stun_endianness.h"


/* Global structures */
IceIPAddress_t destIp[ 10 ] = { 0 };
int destIpLen = 0;
int sockets[ 10 ] = { 0 };
int socketUsed = 0;
IceCandidate_t localCandidates[ICE_MAX_LOCAL_CANDIDATE_COUNT] = { 0x00 };
IceCandidate_t remoteCandidates[ICE_MAX_REMOTE_CANDIDATE_COUNT] = { 0x00 };
IceCandidatePair_t candidatePairs[ICE_MAX_CANDIDATE_PAIR_COUNT] = { 0x00 };
uint8_t stunBuffers[ICE_MAX_CANDIDATE_PAIR_COUNT] = { 0x00 };
IceAgent_t IceAgent;
TransactionIdStore_t TransactionIdStore;
struct pollfd rfds[101] = { 0x00 };
int nfds = 0;
pthread_t thread_id;

static void*
listenForIncomingPackets( void *d )
{
    int retval = 0;
    while( retval <= 0 )
    {
        retval = poll(rfds, nfds, CONNECTION_LISTENER_SOCKET_WAIT_FOR_DATA_TIMEOUT / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }
}

int socketBind( IceIPAddress_t * pHostIpAddress, int sockfd )
{
    int retStatus = 0;
    struct sockaddr_in ipv4Addr;
    struct sockaddr_in6 ipv6Addr;
    struct sockaddr* sockAddr = NULL;
    socklen_t addrLen;

    char ipAddrStr[KVS_IP_ADDRESS_STRING_BUFFER_LEN];

    if ( pHostIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 ) {
        memset(&ipv4Addr, 0x00, sizeof(ipv4Addr));
        ipv4Addr.sin_family = AF_INET;
        ipv4Addr.sin_port = 0; // use next available port
        memcpy(&ipv4Addr.sin_addr, pHostIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE);
        sockAddr = (struct sockaddr*) &ipv4Addr;
        addrLen = sizeof(struct sockaddr_in);

    } else {
        memset(&ipv6Addr, 0x00, sizeof(ipv6Addr));
        ipv6Addr.sin6_family = AF_INET6;
        ipv6Addr.sin6_port = 0; // use next available port
        memcpy(&ipv6Addr.sin6_addr, pHostIpAddress->ipAddress.address, STUN_IPV4_ADDRESS_SIZE);
        sockAddr = (struct sockaddr*) &ipv6Addr;
        addrLen = sizeof(struct sockaddr_in6);
    }

    if (bind(sockfd, sockAddr, addrLen) < 0) {
        printf("bind() failed for ip%s address: %s, port %u with errno %s", IS_IPV4_ADDR(pHostIpAddress->ipAddress) ? "" : "V6", ipAddrStr,
              pHostIpAddress->ipAddress.port, strerror(errno));
    }

    if (getsockname(sockfd, sockAddr, &addrLen) < 0) {
        printf("getsockname() failed with errno %s", strerror(errno));

    }

    pHostIpAddress->ipAddress.port = (uint16_t) pHostIpAddress->ipAddress.family == STUN_ADDRESS_IPv4 ? ipv4Addr.sin_port : ipv6Addr.sin6_port;

    return retStatus;
}

int createSocketConnection( IceIPAddress_t * pBindAddr )
{
    int retStatus;

    sockets[ socketUsed ] = socket( pBindAddr->ipAddress.family == STUN_ADDRESS_IPv4 ? AF_INET : AF_INET6, SOCK_DGRAM, 0 );

    if ( sockets[ socketUsed ] == -1) 
    {
        printf("socket() failed to create socket with errno %s", strerror(errno));
    }
    
    if( pBindAddr )
    {
        retStatus = socketBind( pBindAddr, sockets[ socketUsed ] );
    }
    if( !retStatus )
    {
        socketUsed++;
        rfds[nfds].fd = sockets[ socketUsed - 1 ];
        rfds[nfds].events = POLLIN | POLLPRI;
        rfds[nfds].revents = 0;
        nfds++;
    }
    return retStatus;
}

void getLocalIPAdresses( void )
{
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    struct sockaddr_in6 *saIPv6;
    struct sockaddr_in* pIpv4Addr = NULL;
    struct sockaddr_in6* pIpv6Addr = NULL;
    char addr[ 16 ];

    getifaddrs (&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET ) 
        {
            destIp[ destIpLen ].ipAddress.family = STUN_ADDRESS_IPv4;
            destIp[ destIpLen ].ipAddress.port = 0;
            pIpv4Addr = (struct sockaddr_in*) ifa->ifa_addr;
            memcpy( destIp[ destIpLen ].ipAddress.address , &pIpv4Addr->sin_addr, STUN_IPV4_ADDRESS_SIZE );
            destIp[ destIpLen ].isPointToPoint = ((ifa->ifa_flags & IFF_POINTOPOINT) != 0);
            destIpLen++;
        }
        else if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET6)
        {
            getnameinfo( ifa->ifa_addr, sizeof(struct sockaddr_in6), addr, sizeof(addr), NULL, 0, NI_NUMERICHOST );
            destIp[ destIpLen ].ipAddress.family = STUN_ADDRESS_IPv6;
            destIp[ destIpLen ].ipAddress.port = 0;
            pIpv6Addr = (struct sockaddr_in6*) ifa->ifa_addr;
            memcpy( destIp[ destIpLen ].ipAddress.address , &pIpv6Addr->sin6_addr, STUN_IPV6_ADDRESS_SIZE );
            destIp[ destIpLen ].isPointToPoint = ((ifa->ifa_flags & IFF_POINTOPOINT) != 0);
            destIpLen++;
        }
    }

    freeifaddrs(ifap);
    return ;
}

int IceController_AddHostCandidates( IceAgent_t * pIceAgent )
{
    int retStatus;
    int i;
    getLocalIPAdresses();

    for( i = 0; i < destIpLen-1; i++ )
    {
        retStatus = createSocketConnection( &destIp[ i ] );

        if( retStatus == 0 )
        {
            
            retStatus = Ice_AddHostCandidate( destIp[ i ], pIceAgent );
            if( retStatus != 0 )
            {
                printf(" Host Candidate addition failed\n ");
            }
        }
        else
        {
            printf("Socket creation failed\n");
        }
    }

}

int IceController_Init( char * localName, char * remoteName, char * localPwd, char * remotePwd )
{
    int retStatus;
    int i;
    char combinedName[ MAX_ICE_CONFIG_USER_NAME_LEN ];
    strcat( combinedName, localName );
    strcat( combinedName, remoteName );

    retStatus = Ice_CreateIceAgent( &IceAgent, localName, localPwd, remoteName, remotePwd, combinedName, &TransactionIdStore );

    if( retStatus == 0 )
    {
        for( i = 0; i < ICE_MAX_LOCAL_CANDIDATE_COUNT; i++ )
        {
            IceAgent.localCandidates[i] = &localCandidates[i];
        }
        for( i = 0; i < ICE_MAX_REMOTE_CANDIDATE_COUNT; i++ )
        {
            IceAgent.remoteCandidates[i] = &remoteCandidates[i];
        }
        for( i = 0; i < ICE_MAX_CANDIDATE_PAIR_COUNT; i++ )
        {
            IceAgent.iceCandidatePairs[i] = &candidatePairs[i];
        }
        for( i = 0; i < ICE_MAX_CANDIDATE_PAIR_COUNT; i++ )
        {
            IceAgent.stunMessageBuffers[i] = &stunBuffers[i];
        }

        retStatus = IceController_AddHostCandidates( &IceAgent );

        pthread_create( &thread_id, NULL, listenForIncomingPackets, &thread_id );
    }
    else
    {
        printf("Creation of Ice Agent failed.\n");
    }

    return retStatus;

}
