#include <time.h>
#include <stdio.h>

#include <assert.h>
#include <unistd.h>

#include "peer_connection.h"
#include "networking_utils.h"
#include "peer_connection_sctp.h"

#if ENABLE_SCTP_DATA_CHANNEL

#define LOW_BYTE( x )   ( ( uint8_t ) ( x ) )
#define HIGH_BYTE( x )  ( ( uint8_t ) ( ( ( uint16_t ) ( x ) >> 8 ) & 0xFF ) )

#define LOW_INT16( x )  ( ( int16_t ) ( x ) )
#define HIGH_INT16( x ) ( ( int16_t ) ( ( ( int16_t ) ( x ) >> 16 ) & 0xFFFF ) )

#define LOW_UINT16( x )  ( ( uint16_t ) ( x ) )
#define HIGH_UINT16( x ) ( ( uint16_t ) ( ( ( uint16_t ) ( x ) >> 16 ) & 0xFFFF ) )

#define MAKE_INT16( a, b ) ( ( int16_t ) ( ( ( uint8_t ) ( ( uint16_t ) ( a ) & 0xff ) ) | ( ( uint16_t ) ( ( uint8_t ) ( ( uint16_t ) ( b ) & 0xff ) ) ) << 8 ) )
#define MAKE_INT32( a, b ) ( ( int32_t ) ( ( ( uint16_t ) ( ( uint32_t ) ( a ) & 0xffff ) ) | ( ( uint32_t ) ( ( uint16_t ) ( ( uint32_t ) ( b ) & 0xffff ) ) ) << 16 ) )
#define MAKE_UINT32( a, b ) ( ( uint32_t ) ( ( ( uint16_t ) ( ( uint32_t ) ( a ) & 0xffff ) ) | ( ( uint32_t ) ( ( uint16_t ) ( ( uint32_t ) ( b ) & 0xffff ) ) ) << 16 ) )

#define MAKE_UINT16( a, b ) ( ( uint16_t ) ( ( ( uint8_t ) ( ( uint16_t ) ( a ) & 0xff ) ) | ( ( uint16_t ) ( ( uint8_t ) ( ( uint16_t ) ( b ) & 0xff ) ) ) << 8 ) )

#define SWAP_INT16( x ) MAKE_INT16( HIGH_BYTE( x ), LOW_BYTE( x ) )

#define SWAP_UINT16( x ) MAKE_UINT16( HIGH_BYTE( x ), LOW_BYTE( x ) )

#define SWAP_INT32( x ) MAKE_INT32( SWAP_INT16( HIGH_INT16( x ) ), SWAP_INT16( LOW_INT16( x ) ) )

#define SWAP_UINT32( x ) MAKE_UINT32( SWAP_UINT16( HIGH_UINT16( x ) ), SWAP_UINT16( LOW_UINT16( x ) ) )

/* Callbacks used by usrsctp */
static int SCTP_OnSCTPOutboundPacket( void * addr,
                                      void * data,
                                      size_t length,
                                      uint8_t tos,
                                      uint8_t set_df );
static int SCTP_OnSCTPInboundPacket( struct socket * sock,
                                     union sctp_sockstore addr,
                                     void * data,
                                     size_t length,
                                     struct sctp_rcvinfo rcv,
                                     int flags,
                                     void * ulp_info );
/*-----------------------------------------------------------*/

typedef void (* putInt16Func)( int16_t *,
                               int16_t );
typedef void (* putInt32Func)( int32_t *,
                               int32_t );
typedef void (* putUInt32Func)( uint32_t *,
                                uint32_t );
typedef int32_t (* getUnalignedInt32Func)( void * );
typedef void (* putUnalignedInt32Func)( void *,
                                        int32_t );
typedef void (* putUnalignedInt16Func)( void *,
                                        int16_t );
/*-----------------------------------------------------------*/

static void vPutInt16NoSwap( int16_t * px,
                             int16_t x )
{
    *px = x;
}

static void vPutInt16Swap( int16_t * px,
                           int16_t x )
{
    *px = SWAP_INT16( x );
}

static void vPutInt32Swap( int32_t * px,
                           int32_t x )
{
    *px = SWAP_INT32( x );
}

static void vPutInt32NoSwap( int32_t * px,
                             int32_t x )
{
    *px = x;
}

static void vPutUInt32Swap( uint32_t * px,
                            uint32_t x )
{
    *px = SWAP_UINT32( x );
}

static void vPutUInt32NoSwap( uint32_t * px,
                              uint32_t x )
{
    *px = x;
}

static int32_t getUnalignedInt32Be( void * pVal )
{
    uint8_t * pSrc = ( uint8_t * ) pVal;
    return MAKE_INT32( MAKE_INT16( *( pSrc + 3 ), *( pSrc + 2 ) ), MAKE_INT16( *( pSrc + 1 ), *pSrc ) );
}

static int32_t getUnalignedInt32Le( void * pVal )
{
    uint8_t * pSrc = ( uint8_t * ) pVal;
    return MAKE_INT32( MAKE_INT16( *pSrc, *( pSrc + 1 ) ), MAKE_INT16( *( pSrc + 2 ), *( pSrc + 3 ) ) );
}

static void vPutUnalignedInt32Le( void * pVal,
                                  int32_t val )
{
    uint8_t * pDst = ( uint8_t * ) pVal;
    pDst[0] = ( int8_t ) val;
    pDst[1] = ( int8_t ) ( val >> 8 );
    pDst[2] = ( int8_t ) ( val >> 16 );
    pDst[3] = ( int8_t ) ( val >> 24 );
}

static void vPutUnalignedInt32Be( void * pVal,
                                  int32_t val )
{
    uint8_t * pDst = ( uint8_t * ) pVal;
    pDst[3] = ( int8_t ) val;
    pDst[2] = ( int8_t ) ( val >> 8 );
    pDst[1] = ( int8_t ) ( val >> 16 );
    pDst[0] = ( int8_t ) ( val >> 24 );
}

static void vPutUnalignedInt16Be( void * pVal,
                                  int16_t val )
{
    uint8_t * pDst = ( uint8_t * ) pVal;
    pDst[1] = ( int8_t ) val;
    pDst[0] = ( int8_t ) ( val >> 8 );
}

putInt16Func putInt16 = vPutInt16NoSwap;
putInt32Func putInt32 = vPutInt32NoSwap;
putUInt32Func putUInt32 = vPutUInt32NoSwap;
getUnalignedInt32Func getUnalignedInt32BigEndian = getUnalignedInt32Be;
getUnalignedInt32Func getUnalignedInt32 = getUnalignedInt32Be;
putUnalignedInt32Func putUnalignedInt32 = vPutUnalignedInt32Be;
putUnalignedInt16Func putUnalignedInt16BigEndian = vPutUnalignedInt16Be;
/*-----------------------------------------------------------*/

static uint8_t isBigEndian( void )
{
    union {
        uint8_t c[4];
        int32_t i;
    } u;

    u.i = 0x01020304;

    return( 0x01 == u.c[0] );
}
/*-----------------------------------------------------------*/

static void vInitializeEndianness( void )
{
    if( isBigEndian() )
    {

        putInt16 = vPutInt16NoSwap;
        putInt32 = vPutInt32NoSwap;
        putUInt32 = vPutUInt32NoSwap;
        getUnalignedInt32 = getUnalignedInt32Be;
        putUnalignedInt32 = vPutUnalignedInt32Be;

    }
    else {

        putInt16 = vPutInt16Swap;
        putInt32 = vPutInt32Swap;
        putUInt32 = vPutUInt32Swap;
        getUnalignedInt32 = getUnalignedInt32Le;
        putUnalignedInt32 = vPutUnalignedInt32Le;

    }
}

/*-----------------------------------------------------------*/

/* Initialise SCTP socket address with SCTP_ASSOCIATION_DEFAULT_PORT
 * and AF_CONN address family. */
static SctpUtilsResult_t ulInitSctpAddrConn( SCTPSession_t * pSctpSession,
                                             struct sockaddr_conn * sconn )
{

    SctpUtilsResult_t retStatus = SCTP_UTILS_RESULT_OK;

    sconn->sconn_family = AF_CONN;
    putInt16( ( int16_t * ) &sconn->sconn_port, SCTP_ASSOCIATION_DEFAULT_PORT );
    sconn->sconn_addr = pSctpSession;


    return retStatus;
}
/*-----------------------------------------------------------*/

/* Configure the SCTP socket with default settings. */
static SctpUtilsResult_t ulConfigureSctpSocket( struct socket * socket )
{

    SctpUtilsResult_t retStatus = SCTP_UTILS_RESULT_OK;
    struct linger linger_opt;
    struct sctp_event event;
    uint32_t i;
    uint32_t valueOn = 1;
    uint16_t event_types[] = {SCTP_ASSOC_CHANGE,   SCTP_PEER_ADDR_CHANGE,      SCTP_REMOTE_ERROR,
                              SCTP_SHUTDOWN_EVENT, SCTP_ADAPTATION_INDICATION, SCTP_PARTIAL_DELIVERY_EVENT};

    if( usrsctp_set_non_blocking( socket, 1 ) != 0 )
    {
        retStatus = SCTP_STATUS_ERR_FAIL;
    }

    /* SCTP_OnSCTPOutboundPacket must not be called after close */
    linger_opt.l_onoff = 1;
    linger_opt.l_linger = 0;
    if( ( retStatus == SCTP_UTILS_RESULT_OK ) && ( usrsctp_setsockopt( socket, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof( linger_opt ) ) != 0 ) )
    {
        LogError( ( " usrsctp_setsockopt failed SOL_SOCKET, SO_LINGER " ) );
        retStatus = SCTP_UTILS_RESULT_FAIL_SET_SOCKET_OPTIONS;
    }

    /* packets are generally sent as soon as possible and no unnecessary */
    /* delays are introduced, at the cost of more packets in the network. */
    if( ( retStatus == SCTP_UTILS_RESULT_OK ) && ( usrsctp_setsockopt( socket, IPPROTO_SCTP, SCTP_NODELAY, &valueOn, sizeof( valueOn ) ) != 0 ) )
    {
        LogError( ( " usrsctp_setsockopt failed: IPPROTO_SCTP, SCTP_NODELAY " ) );
        retStatus = SCTP_UTILS_RESULT_FAIL_SET_SOCKET_OPTIONS;
    }

    memset( &event, 0, sizeof( event ) );
    event.se_assoc_id = SCTP_FUTURE_ASSOC;
    event.se_on = 1;
    for( i = 0; i < ( uint32_t ) ( sizeof( event_types ) / sizeof( uint16_t ) ); i++ ) {
        event.se_type = event_types[i];
        if( ( retStatus == SCTP_UTILS_RESULT_OK ) && ( usrsctp_setsockopt( socket, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof( struct sctp_event ) ) != 0 ) )
        {
            LogError( ( " usrsctp_setsockopt failed: IPPROTO_SCTP, SCTP_EVENT " ) );
            retStatus = SCTP_UTILS_RESULT_FAIL_SET_SOCKET_OPTIONS;
            break;
        }
    }

    struct sctp_initmsg initmsg;
    memset( &initmsg, 0, sizeof( struct sctp_initmsg ) );
    initmsg.sinit_num_ostreams = 300;
    initmsg.sinit_max_instreams = 300;
    if( ( retStatus == SCTP_UTILS_RESULT_OK ) && ( usrsctp_setsockopt( socket, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof( struct sctp_initmsg ) ) != 0 ) )
    {
        LogError( ( " usrsctp_setsockopt failed: IPPROTO_SCTP, SCTP_INITMSG " ) );
        retStatus = SCTP_UTILS_RESULT_FAIL_SET_SOCKET_OPTIONS;
    }


    return retStatus;
}
/*-----------------------------------------------------------*/

/* Initialize the SCTP stack. */
SctpUtilsResult_t SCTP_InitSCTPSession( void )
{
    SctpUtilsResult_t retStatus = SCTP_UTILS_RESULT_OK;

    vInitializeEndianness();

    usrsctp_init_nothreads( 0, &SCTP_OnSCTPOutboundPacket, NULL );

    /* Disable Explicit Congestion Notification */
    usrsctp_sysctl_set_sctp_ecn_enable( 0 );

    return retStatus;
}
/*-----------------------------------------------------------*/

/* De initialize the SCTP stack. */
void SCTP_DeInitSCTPSession( void )
{
    /* need to block until usrsctp_finish or sctp thread could be calling free objects and cause segfault */
    while( usrsctp_finish() != 0 ) {
        usleep( DEFAULT_USRSCTP_TEARDOWN_POLLING_INTERVAL_USEC );
    }
}
/*-----------------------------------------------------------*/

/* Create the SCTP session by connecting to the remote socket */
SctpUtilsResult_t SCTP_CreateSCTPSession( SCTPSession_t * pSctpSession )
{
    SctpUtilsResult_t retStatus = SCTP_UTILS_RESULT_OK;
    struct sockaddr_conn localConn, remoteConn;
    struct sctp_paddrparams params;
    int32_t connectStatus = 0;

    assert( pSctpSession != NULL );

    memset( &params, 0x00, sizeof( struct sctp_paddrparams ) );
    memset( &localConn, 0x00, sizeof( struct sockaddr_conn ) );
    memset( &remoteConn, 0x00, sizeof( struct sockaddr_conn ) );

    pSctpSession->shutdownStatus = SCTP_SESSION_ACTIVE;

    ulInitSctpAddrConn( pSctpSession, &localConn );
    ulInitSctpAddrConn( pSctpSession, &remoteConn );

    if( ( pSctpSession->socket = usrsctp_socket( AF_CONN, SOCK_STREAM, IPPROTO_SCTP, SCTP_OnSCTPInboundPacket, NULL, 0, pSctpSession ) ) == NULL )
    {
        retStatus = SCTP_UTILS_RESULT_FAIL;
    }

    usrsctp_register_address( pSctpSession );
    if( ulConfigureSctpSocket( pSctpSession->socket ) != SCTP_UTILS_RESULT_OK )
    {
        retStatus = SCTP_UTILS_RESULT_FAIL;
    }

    if( usrsctp_bind( pSctpSession->socket, ( struct sockaddr * ) &localConn, sizeof( localConn ) ) != 0 )
    {
        retStatus = SCTP_UTILS_RESULT_FAIL;
    }

    connectStatus = usrsctp_connect( pSctpSession->socket, ( struct sockaddr * ) &remoteConn, sizeof( remoteConn ) );
    if( !( ( connectStatus >= 0 ) || ( errno == EINPROGRESS ) ) )
    {
        retStatus = SCTP_UTILS_RESULT_FAIL;
    }

    memcpy( &params.spp_address, &remoteConn, sizeof( remoteConn ) );
    params.spp_flags = SPP_PMTUD_DISABLE;
    params.spp_pathmtu = SCTP_MTU;
    if( ( usrsctp_setsockopt( pSctpSession->socket, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &params, sizeof( params ) ) != 0 ) )
    {
        retStatus = SCTP_UTILS_RESULT_FAIL;
    }


    if( retStatus != SCTP_UTILS_RESULT_OK )
    {
        SCTP_FreeSCTPSession( pSctpSession );
    }

    return retStatus;
}
/*-----------------------------------------------------------*/

/* Free the SCTP session */
SctpUtilsResult_t SCTP_FreeSCTPSession( SCTPSession_t * pSctpSession )
{

    SctpUtilsResult_t retStatus = SCTP_UTILS_RESULT_OK;
    uint64_t shutdownTimeout;

    usrsctp_deregister_address( pSctpSession );
    /* handle issue mentioned here: https://github.com/sctplab/usrsctp/issues/147
     * the change in shutdownStatus will trigger SCTP_OnSCTPOutboundPacket to return -1 */
    pSctpSession->shutdownStatus = SCTP_SESSION_SHUTDOWN_INITIATED;

    if( pSctpSession->socket != NULL )
    {
        usrsctp_set_ulpinfo( pSctpSession->socket, NULL );
        usrsctp_shutdown( pSctpSession->socket, SHUT_RDWR );
        usrsctp_close( pSctpSession->socket );
    }

    shutdownTimeout = NetworkingUtils_GetCurrentTimeUs( NULL ) + SECONDS_TO_USEC( DEFAULT_SCTP_SHUTDOWN_TIMEOUT_SECONDS );
    while( ( ( pSctpSession->shutdownStatus ) != SCTP_SESSION_SHUTDOWN_COMPLETED ) && ( NetworkingUtils_GetCurrentTimeUs( NULL ) < shutdownTimeout ) ) {
        usleep( DEFAULT_USRSCTP_TEARDOWN_POLLING_INTERVAL_USEC );
    }

    return retStatus;
}
/*-----------------------------------------------------------*/

/* Write SCTP message to the given session and stream */
SctpUtilsResult_t SCTP_WriteMessageSCTPSession( SCTPSession_t * pSctpSession,
                                                uint32_t streamId,
                                                uint8_t isBinary,
                                                uint8_t * pMessage,
                                                uint32_t pMessageLen )
{

    SctpUtilsResult_t retStatus = SCTP_UTILS_RESULT_OK;

    if( ( pMessage == NULL ) || ( pSctpSession == NULL ) )
    {
        LogError( ( "No message or pDataChannel received in onDataChannelMessage" ) );
        return 1;
    }

    memset( &pSctpSession->spa, 0x00, sizeof( struct sctp_sendv_spa ) );

    pSctpSession->spa.sendv_flags |= SCTP_SEND_SNDINFO_VALID;
    pSctpSession->spa.sendv_sndinfo.snd_sid = streamId;

    if( ( pSctpSession->packet[1] & DCEP_DATA_CHANNEL_RELIABLE_UNORDERED ) != 0 )
    {
        pSctpSession->spa.sendv_sndinfo.snd_flags |= SCTP_UNORDERED;
    }
    if( ( pSctpSession->packet[1] & DCEP_DATA_CHANNEL_REXMIT ) != 0 )
    {
        pSctpSession->spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
        pSctpSession->spa.sendv_prinfo.pr_value = getUnalignedInt32BigEndian( ( void * ) ( pSctpSession->packet + sizeof( uint32_t ) ) );
    }
    if( ( pSctpSession->packet[1] & DCEP_DATA_CHANNEL_TIMED ) != 0 )
    {
        pSctpSession->spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
        pSctpSession->spa.sendv_prinfo.pr_value = getUnalignedInt32BigEndian( ( void * ) ( pSctpSession->packet + sizeof( uint32_t ) ) );
    }

    putInt32( ( int32_t * ) &pSctpSession->spa.sendv_sndinfo.snd_ppid, isBinary ? SCTP_PPID_BINARY : SCTP_PPID_STRING );
    if( usrsctp_sendv( pSctpSession->socket, pMessage, pMessageLen, NULL, 0, &pSctpSession->spa, sizeof( pSctpSession->spa ), SCTP_SENDV_SPA, 0 ) <= 0 )
    {
        LogError( ( "usrsctp_sendv internal error" ) );
        retStatus = SCTP_UTILS_RESULT_FAIL;
    }


    return retStatus;
}
/*-----------------------------------------------------------*/

/* Create and send DCEP DATA_CHANNEL_OPEN Message */
/*
 * https://tools.ietf.org/html/draft-ietf-rtcweb-data-protocol-09#section-5.1
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |  Message Type |  Channel Type |            Priority           |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                    Reliability Parameter                      |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |         Label Length          |       Protocol Length         |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     \                                                               /
 *     |                             Label                             |
 *     /                                                               \
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     \                                                               /
 *     |                            Protocol                           |
 *     /                                                               \
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
SctpUtilsResult_t SCTP_WriteDCEPSCTPSession( SCTPSession_t * pSctpSession,
                                             uint32_t streamId,
                                             char * pChannelName,
                                             uint32_t pChannelNameLen,
                                             DataChannelInit_t * pDataChannelInit )
{

    SctpUtilsResult_t retStatus = 0;

    if( !( ( pSctpSession != NULL ) && ( pChannelName != NULL ) ) )
    {
        retStatus = SCTP_UTILS_RESULT_BAD_PARAMETER;
    }
    else
    {
        memset( &pSctpSession->spa, 0x00, sizeof( struct sctp_sendv_spa ) );
        memset( pSctpSession->packet, 0x00, sizeof( pSctpSession->packet ) );
        pSctpSession->packetSize = SCTP_DCEP_HEADER_LENGTH + pChannelNameLen;

        /* Setting the fields of DATA_CHANNEL_OPEN message */
        pSctpSession->packet[0] = DCEP_DATA_CHANNEL_OPEN; /* message type */

        /* Set Channel type based on supplied parameters */
        pSctpSession->packet[1] = DCEP_DATA_CHANNEL_RELIABLE_ORDERED;

        /*
         *   Set channel type and reliability parameters based on input
         *   SCTP allows fine tuning the channel robustness:
         *      1. Ordering: The data packets can be sent out in an ordered/unordered fashion
         *      2. Reliability: This determines how the retransmission of packets is handled.
         *   There are 2 parameters that can be fine tuned to achieve this:
         *      a. Number of retransmits
         *      b. Packet lifetime
         *   Default values for the parameters is 0. This falls back to reliable channel
         */

        if( !pDataChannelInit->ordered )
        {
            pSctpSession->packet[1] |= DCEP_DATA_CHANNEL_RELIABLE_UNORDERED;
        }
        if( ( pDataChannelInit->maxRetransmits.value >= 0 ) && ( pDataChannelInit->maxRetransmits.isNull == 0U ) )
        {
            pSctpSession->packet[1] |= DCEP_DATA_CHANNEL_REXMIT;
            vPutUnalignedInt32Be( pSctpSession->packet + sizeof( uint32_t ), pDataChannelInit->maxRetransmits.value );
        }
        else if( ( pDataChannelInit->maxPacketLifeTime.value >= 0 ) && ( pDataChannelInit->maxPacketLifeTime.isNull == 0U ) )
        {
            pSctpSession->packet[1] |= DCEP_DATA_CHANNEL_TIMED;
            vPutUnalignedInt32Be( pSctpSession->packet + sizeof( uint32_t ), pDataChannelInit->maxPacketLifeTime.value );
        }

        putUnalignedInt16BigEndian( pSctpSession->packet + SCTP_DCEP_LABEL_LEN_OFFSET, pChannelNameLen );
        memcpy( pSctpSession->packet + SCTP_DCEP_LABEL_OFFSET, pChannelName, pChannelNameLen );
        pSctpSession->spa.sendv_flags |= SCTP_SEND_SNDINFO_VALID;
        pSctpSession->spa.sendv_sndinfo.snd_sid = streamId;

        putUInt32( ( uint32_t * ) &pSctpSession->spa.sendv_sndinfo.snd_ppid, SCTP_PPID_DCEP );

        if( usrsctp_sendv( pSctpSession->socket, pSctpSession->packet, pSctpSession->packetSize,
                           NULL, 0, &pSctpSession->spa, sizeof( pSctpSession->spa ), SCTP_SENDV_SPA, 0 ) > 0 )
        {
            retStatus = SCTP_UTILS_RESULT_FAIL;
        }
    }

    return retStatus;
}
/*-----------------------------------------------------------*/

/* Callback passed to the SCTP stack when an outbound packet is
 * ready for processing */
int SCTP_OnSCTPOutboundPacket( void * addr,
                               void * data,
                               size_t length,
                               uint8_t tos,
                               uint8_t set_df )
{
    ( void ) ( tos );
    ( void ) ( set_df );

    SCTPSession_t * pSctpSession = ( SCTPSession_t * ) addr;

    if( ( pSctpSession == NULL ) || ( ( pSctpSession->shutdownStatus ) == SCTP_SESSION_SHUTDOWN_INITIATED ) ||
        ( pSctpSession->sctpSessionCallbacks.outboundPacketFunc == NULL ) )
    {
        if( pSctpSession != NULL )
        {
            pSctpSession->shutdownStatus = SCTP_SESSION_SHUTDOWN_COMPLETED;
        }
        return -1;
    }

    /* Call the session and channel specific callback configured by the peer
     * connection */
    pSctpSession->sctpSessionCallbacks.outboundPacketFunc( pSctpSession->sctpSessionCallbacks.customData, data, length );

    return 0;
}

/* Pass a decrypted DTLS packet to the SCTP stack for further processing
 * of SCTP specific stuff */
SctpUtilsResult_t SCTP_PutSCTPPacket( SCTPSession_t * pSctpSession,
                                      uint8_t * buf,
                                      uint32_t bufLen )
{

    SctpUtilsResult_t retStatus = SCTP_UTILS_RESULT_OK;

    usrsctp_conninput( pSctpSession, buf, bufLen, 0 );

    return retStatus;
}
/*-----------------------------------------------------------*/

/* Handle an incoming DCEP DATA_CHANNEL_OPEN Message */
static SctpUtilsResult_t ulHandleDCEPPacket( SCTPSession_t * pSctpSession,
                                             uint32_t streamId,
                                             uint8_t * data,
                                             size_t length )
{

    SctpUtilsResult_t retStatus = SCTP_UTILS_RESULT_OK;
    uint16_t labelLength = 0;
    uint16_t protocolLength = 0;

    /* Assert that is DCEP of type DataChannelOpen */
    if( ( length <= SCTP_DCEP_HEADER_LENGTH ) && ( data[0] != DCEP_DATA_CHANNEL_OPEN ) )
    {
        retStatus = SCTP_UTILS_RESULT_FAIL_INVALID_DCEP_PACKET;
    }

    memcpy( &labelLength, data + 8, sizeof( uint16_t ) );
    memcpy( &protocolLength, data + 10, sizeof( uint16_t ) );
    putInt16( ( int16_t * ) &labelLength, labelLength );
    putInt16( ( int16_t * ) &protocolLength, protocolLength );

    if( ( !( labelLength + protocolLength + SCTP_DCEP_HEADER_LENGTH ) ) >= length )
    {
        retStatus = SCTP_UTILS_RESULT_FAIL_INVALID_DCEP_PACKET;
    }

    if( !( SCTP_MAX_ALLOWABLE_PACKET_LENGTH >= length ) )
    {
        retStatus = SCTP_UTILS_RESULT_FAIL_INVALID_DCEP_PACKET;
    }

    if( retStatus == SCTP_UTILS_RESULT_OK )
    {
        /* On valid DCEP DATA_CHANNEL_OPEN Message call peer connection configured
         * callback to allocate and open the data channel */
        pSctpSession->sctpSessionCallbacks.dataChannelOpenFunc( pSctpSession->sctpSessionCallbacks.customData, streamId, data + SCTP_DCEP_HEADER_LENGTH,
                                                                labelLength );
    }

    return retStatus;
}
/*-----------------------------------------------------------*/

/* Process an incoming SCTP packet, this API is passed as a callback to the
 * SCTP stack to be called while there is a valid packet ready. */
int SCTP_OnSCTPInboundPacket( struct socket * sock,
                              union sctp_sockstore addr,
                              void * data,
                              size_t length,
                              struct sctp_rcvinfo rcv,
                              int flags,
                              void * ulp_info )
{
    ( void )( sock );
    ( void )( addr );
    ( void )( flags );
    int retStatus = 1;
    SCTPSession_t * pSctpSession = ( SCTPSession_t * ) ulp_info;
    uint8_t isBinary = 0U;

    rcv.rcv_ppid = ntohl( rcv.rcv_ppid );
    switch( rcv.rcv_ppid ) {
        /* Process incoming DCEP DATA_CHANNEL_OPEN Message */
        case SCTP_PPID_DCEP:
            if( ulHandleDCEPPacket( pSctpSession, rcv.rcv_sid, data, length ) != SCTP_UTILS_RESULT_OK )
            {
                retStatus = 1;
            }
            break;
        /* Process incoming application data */
        case SCTP_PPID_BINARY:
        case SCTP_PPID_BINARY_EMPTY:
            isBinary = true;
        /* fallthrough */
        case SCTP_PPID_STRING:
        case SCTP_PPID_STRING_EMPTY:
            pSctpSession->sctpSessionCallbacks.dataChannelMessageFunc( pSctpSession->sctpSessionCallbacks.customData, rcv.rcv_sid, isBinary, data,
                                                                       length );
            break;
        default:
            LogInfo( ( "Unhandled PPID on incoming SCTP message %ld", ( long unsigned ) rcv.rcv_ppid ) );
            break;
    }

    /*
     * IMPORTANT!!! The allocation is done in the sctp library using default allocator
     * so we need to use the default free API.
     */
    if( data != NULL )
    {
        free( data );
    }
    return retStatus;
}
/*-----------------------------------------------------------*/

/* Send SCTP stream reset to close the data channel */
SctpUtilsResult_t SCTP_StreamReset( SCTPSession_t * pSctpSession,
                                    uint32_t streamID )
{
    SctpUtilsResult_t ret = SCTP_UTILS_RESULT_OK;
    struct sctp_reset_streams * pSrs;
    size_t len;
    uint8_t ucSRSBuffer[64] = { 0 };


    len = sizeof( sctp_assoc_t ) + ( ( 2 + 1 ) * sizeof( uint16_t ) );

    if( len > 64 )
    {
        ret = SCTP_UTILS_RESULT_FAIL;
    }
    else
    {
        int iSetSockReturn;
        pSrs = ( struct sctp_reset_streams * ) ucSRSBuffer;

        pSrs->srs_flags = SCTP_STREAM_RESET_OUTGOING;
        pSrs->srs_number_streams = 1U;
        pSrs->srs_stream_list[0] = streamID;
        iSetSockReturn = usrsctp_setsockopt( pSctpSession->socket, IPPROTO_SCTP, SCTP_RESET_STREAMS, pSrs, ( socklen_t )len );
        if( iSetSockReturn < 0 )
        {
            ret = SCTP_UTILS_RESULT_FAIL_CLOSE_DATA_CHANNEL;
            LogDebug( ( "Error closing the data channel stream, usrsctp_setsockopt returns: %d", iSetSockReturn ) );
        }
    }

    return ret;
}
/*-----------------------------------------------------------*/

#endif /* ENABLE_SCTP_DATA_CHANNEL */
