#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "demo_config.h"
#include "logging.h"
#include "demo_data_types.h"
#include "signaling_controller.h"
#include "sdp_controller.h"
#include "string_utils.h"
#include "metric.h"

#if ENABLE_SCTP_DATA_CHANNEL
#include "peer_connection_sctp.h"
#endif /* ENABLE_SCTP_DATA_CHANNEL */

#define AWS_DEFAULT_STUN_SERVER_URL_POSTFIX "amazonaws.com"
#define AWS_DEFAULT_STUN_SERVER_URL_POSTFIX_CN "amazonaws.com.cn"
#define AWS_DEFAULT_STUN_SERVER_URL "stun.kinesisvideo.%s.%s"

#define IS_USERNAME_FOUND_BIT ( 1 << 0 )
#define IS_PASSWORD_FOUND_BIT ( 1 << 1 )
#define SET_REMOTE_INFO_USERNAME_FOUND( isFoundBit ) ( isFoundBit |= IS_USERNAME_FOUND_BIT )
#define SET_REMOTE_INFO_PASSWORD_FOUND( isFoundBit ) ( isFoundBit |= IS_PASSWORD_FOUND_BIT )
#define IS_REMOTE_INFO_ALL_FOUND( isFoundBit ) ( isFoundBit & IS_USERNAME_FOUND_BIT && isFoundBit & IS_PASSWORD_FOUND_BIT )

#define DEMO_JSON_CANDIDATE_MAX_LENGTH ( 512 )

#define DEMO_CANDIDATE_TYPE_HOST_STRING "host"
#define DEMO_CANDIDATE_TYPE_SRFLX_STRING "srflx"
#define DEMO_CANDIDATE_TYPE_PRFLX_STRING "prflx"
#define DEMO_CANDIDATE_TYPE_RELAY_STRING "relay"
#define DEMO_CANDIDATE_TYPE_UNKNOWN_STRING "unknown"

#define DEMO_ICE_CANDIDATE_JSON_TEMPLATE "{\"candidate\":\"%.*s\",\"sdpMid\":\"0\",\"sdpMLineIndex\":0}"
#define DEMO_ICE_CANDIDATE_JSON_MAX_LENGTH ( 1024 )
#define DEMO_ICE_CANDIDATE_JSON_IPV4_TEMPLATE "candidate:%lu 1 udp %u %d.%d.%d.%d %d typ %s raddr 0.0.0.0 rport 0 generation 0 network-cost 999"
#define DEMO_ICE_CANDIDATE_JSON_IPV6_TEMPLATE "candidate:%lu 1 udp %u %02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X " \
                                              "%d typ %s raddr ::/0 rport 0 generation 0 network-cost 999"

#define ICE_SERVER_TYPE_STUN "stun:"
#define ICE_SERVER_TYPE_STUN_LENGTH ( 5 )
#define ICE_SERVER_TYPE_TURN "turn:"
#define ICE_SERVER_TYPE_TURN_LENGTH ( 5 )
#define ICE_SERVER_TYPE_TURNS "turns:"
#define ICE_SERVER_TYPE_TURNS_LENGTH ( 6 )

#define SIGNALING_CONNECT_STATE_TIMEOUT_SEC ( 15 )

DemoContext_t demoContext;

static int OnSignalingMessageReceived( SignalingMessage_t * pSignalingMessage,
                                       void * pUserData );
static int32_t InitializePeerConnectionSession( DemoContext_t * pDemoContext,
                                                DemoPeerConnectionSession_t * pDemoSession );
static int32_t StartPeerConnectionSession( DemoContext_t * pDemoContext,
                                           DemoPeerConnectionSession_t * pDemoSession,
                                           const char * pRemoteClientId,
                                           size_t remoteClientIdLength );
static DemoPeerConnectionSession_t * GetCreatePeerConnectionSession( DemoContext_t * pDemoContext,
                                                                     const char * pRemoteClientId,
                                                                     size_t remoteClientIdLength,
                                                                     uint8_t allowCreate );
static void HandleRemoteCandidate( DemoContext_t * pDemoContext,
                                   const SignalingMessage_t * pSignalingMessage );
static void HandleIceServerReconnect( DemoContext_t * pDemoContext,
                                      const SignalingMessage_t * pSignalingMessage );
static void HandleLocalCandidateReady( void * pCustomContext,
                                       PeerConnectionIceLocalCandidate_t * pIceLocalCandidate );
static void HandleSdpOffer( DemoContext_t * pDemoContext,
                            const SignalingMessage_t * pSignalingMessage );
static const char * GetCandidateTypeString( IceCandidateType_t candidateType );

static void terminateHandler( int sig )
{
    //SignalingController_Deinit( &demoContext.signalingControllerContext );
    //IceController_Deinit( &demoContext.iceControllerContext );
    exit( 0 );
}

static int32_t OnMediaSinkHook( void * pCustom,
                                webrtc_frame_t * pFrame )
{
    int32_t ret = 0;
    DemoContext_t * pDemoContext = ( DemoContext_t * ) pCustom;
    PeerConnectionResult_t peerConnectionResult;
    Transceiver_t * pTransceiver = NULL;
    PeerConnectionFrame_t peerConnectionFrame;
    int i;

    if( ( pDemoContext == NULL ) || ( pFrame == NULL ) )
    {
        LogError( ( "Invalid input, pCustom: %p, pFrame: %p", pCustom, pFrame ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        peerConnectionFrame.version = PEER_CONNECTION_FRAME_CURRENT_VERSION;
        peerConnectionFrame.presentationUs = pFrame->timestampUs;
        peerConnectionFrame.pData = pFrame->pData;
        peerConnectionFrame.dataLength = pFrame->size;

        for( i = 0; i < AWS_MAX_VIEWER_NUM; i++ )
        {
            if( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO )
            {
                pTransceiver = &pDemoContext->peerConnectionSessions[ i ].transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO ];
            }
            else if( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_AUDIO )
            {
                pTransceiver = &pDemoContext->peerConnectionSessions[ i ].transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO ];
            }
            else
            {
                /* Unknown kind, skip that. */
                LogWarn( ( "Unknown track kind: %d", pFrame->trackKind ) );
                break;
            }

            if( pDemoContext->peerConnectionSessions[ i ].peerConnectionSession.state == PEER_CONNECTION_SESSION_STATE_CONNECTION_READY )
            {
                peerConnectionResult = PeerConnection_WriteFrame( &pDemoContext->peerConnectionSessions[ i ].peerConnectionSession,
                                                                  pTransceiver,
                                                                  &peerConnectionFrame );

                if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
                {
                    LogError( ( "Fail to write %s frame, result: %d", ( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO ) ? "video" : "audio",
                                peerConnectionResult ) );
                    ret = -3;
                }
            }
        }
    }

    return ret;
}

static int32_t InitializeAppMediaSource( DemoContext_t * pDemoContext )
{
    int32_t ret = 0;

    if( pDemoContext == NULL )
    {
        LogError( ( "Invalid input, pDemoContext: %p", pDemoContext ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        ret = AppMediaSource_Init( &pDemoContext->appMediaSourcesContext,
                                   OnMediaSinkHook,
                                   pDemoContext );
    }

    return ret;
}


static int32_t ParseIceServerUri( IceControllerIceServer_t * pIceServer,
                                  char * pUri,
                                  size_t uriLength )
{
    int32_t ret = 0;
    StringUtilsResult_t retString;
    const char * pCurr, * pTail, * pNext;
    uint32_t port, portStringLength;

    /* Example Ice server URI:
     *  1. turn:35-94-7-249.t-490d1050.kinesisvideo.us-west-2.amazonaws.com:443?transport=udp
     *  2. stun:stun.kinesisvideo.us-west-2.amazonaws.com:443 */
    if( ( uriLength > ICE_SERVER_TYPE_STUN_LENGTH ) && ( strncmp( ICE_SERVER_TYPE_STUN,
                                                                  pUri,
                                                                  ICE_SERVER_TYPE_STUN_LENGTH ) == 0 ) )
    {
        pIceServer->serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_STUN;
        pTail = pUri + uriLength;
        pCurr = pUri + ICE_SERVER_TYPE_STUN_LENGTH;
    }
    else if( ( ( uriLength > ICE_SERVER_TYPE_TURNS_LENGTH ) && ( strncmp( ICE_SERVER_TYPE_TURNS,
                                                                          pUri,
                                                                          ICE_SERVER_TYPE_TURNS_LENGTH ) == 0 ) ) )
    {
        pIceServer->serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_TURNS;
        pTail = pUri + uriLength;
        pCurr = pUri + ICE_SERVER_TYPE_TURNS_LENGTH;
    }
    else if( ( uriLength > ICE_SERVER_TYPE_TURN_LENGTH ) && ( strncmp( ICE_SERVER_TYPE_TURN,
                                                                       pUri,
                                                                       ICE_SERVER_TYPE_TURN_LENGTH ) == 0 ) )
    {
        pIceServer->serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_TURN;
        pTail = pUri + uriLength;
        pCurr = pUri + ICE_SERVER_TYPE_TURN_LENGTH;
    }
    else
    {
        /* Invalid server URI, drop it. */
        LogWarn( ( "Unable to parse Ice URI, drop it, URI: %.*s", ( int ) uriLength, pUri ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        pNext = memchr( pCurr,
                        ':',
                        pTail - pCurr );
        if( pNext == NULL )
        {
            LogWarn( ( "Unable to find second ':', drop it, URI: %.*s", ( int ) uriLength, pUri ) );
            ret = -1;
        }
        else
        {
            if( pNext - pCurr >= ICE_CONTROLLER_ICE_SERVER_URL_MAX_LENGTH )
            {
                LogWarn( ( "URL buffer is not enough to store Ice URL, length: %ld, URI: %.*s",
                           pNext - pCurr,
                           ( int ) uriLength, pUri ) );
                ret = -1;
            }
            else
            {
                memcpy( pIceServer->url,
                        pCurr,
                        pNext - pCurr );
                pIceServer->urlLength = pNext - pCurr;
                /* Note that URL must be NULL terminated for DNS lookup. */
                pIceServer->url[ pIceServer->urlLength ] = '\0';
                pCurr = pNext + 1;
            }
        }
    }

    if( ( ret == 0 ) && ( pCurr <= pTail ) )
    {
        pNext = memchr( pCurr,
                        '?',
                        pTail - pCurr );
        if( pNext == NULL )
        {
            portStringLength = pTail - pCurr;
        }
        else
        {
            portStringLength = pNext - pCurr;
        }

        retString = StringUtils_ConvertStringToUl( pCurr,
                                                   portStringLength,
                                                   &port );
        if( ( retString != STRING_UTILS_RESULT_OK ) || ( port > UINT16_MAX ) )
        {
            LogWarn( ( "No valid port number, parsed string: %.*s", ( int ) portStringLength, pCurr ) );
            ret = -1;
        }
        else
        {
            pIceServer->iceEndpoint.transportAddress.port = ( uint16_t ) port;
            pCurr += portStringLength;
        }
    }

    if( ret == 0 )
    {
        if( pCurr >= pTail )
        {
            LogWarn( ( "No valid transport string found" ) );
            ret = -1;
        }
        else if( ( pIceServer->serverType == ICE_CONTROLLER_ICE_SERVER_TYPE_TURN ) ||
                 ( pIceServer->serverType == ICE_CONTROLLER_ICE_SERVER_TYPE_TURNS ) )
        {
            if( strncmp( pCurr,
                         "?transport=udp",
                         pTail - pCurr ) == 0 )
            {
                pIceServer->protocol = ICE_SOCKET_PROTOCOL_UDP;
            }
            else if( strncmp( pCurr,
                              "?transport=tcp",
                              pTail - pCurr ) == 0 )
            {
                pIceServer->protocol = ICE_SOCKET_PROTOCOL_TCP;
            }
            else
            {
                LogWarn( ( "Unknown transport string found, protocol: %.*s", ( int )( pTail - pCurr ), pCurr ) );
                ret = -1;
            }
        }
        else
        {
            /* Do nothing, coverity happy. */
        }
    }

    return ret;
}

static int32_t GetIceServerList( DemoContext_t * pDemoContext,
                                 IceControllerIceServer_t * pOutputIceServers,
                                 size_t * pOutputIceServersCount )
{
    int32_t skipProcess = 0;
    int32_t parseResult = 0;
    SignalingControllerResult_t signalingControllerReturn;
    IceServerConfig_t * pIceServerConfigs;
    size_t iceServerConfigsCount;
    char * pStunUrlPostfix;
    int written;
    uint32_t i, j;
    size_t currentIceServerIndex = 0U;

    if( ( pDemoContext == NULL ) ||
        ( pOutputIceServers == NULL ) ||
        ( pOutputIceServersCount == NULL ) )
    {
        LogError( ( "Invalid input, pDemoContext: %p, pOutputIceServers: %p, pOutputIceServersCount: %p",
                    pDemoContext,
                    pOutputIceServers,
                    pOutputIceServersCount ) );
        skipProcess = 1;
    }
    else if( *pOutputIceServersCount < 1 )
    {
        /* At least one space for default STUN server. */
        LogError( ( "Invalid input, buffer size(%lu) is insufficient",
                    *pOutputIceServersCount ) );
        skipProcess = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    if( skipProcess == 0 )
    {
        signalingControllerReturn = SignalingController_QueryIceServerConfigs( &pDemoContext->signalingControllerContext,
                                                                               &pIceServerConfigs,
                                                                               &iceServerConfigsCount );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to get Ice server configs, result: %d", signalingControllerReturn ) );
            skipProcess = -1;
        }
    }

    if( skipProcess == 0 )
    {
        /* Put the default STUN server into index 0. */
        if( strstr( AWS_REGION,
                    "cn-" ) )
        {
            pStunUrlPostfix = AWS_DEFAULT_STUN_SERVER_URL_POSTFIX_CN;
        }
        else
        {
            pStunUrlPostfix = AWS_DEFAULT_STUN_SERVER_URL_POSTFIX;
        }

        /* Get the default STUN server. */
        written = snprintf( pOutputIceServers[ currentIceServerIndex ].url,
                            ICE_CONTROLLER_ICE_SERVER_URL_MAX_LENGTH,
                            AWS_DEFAULT_STUN_SERVER_URL,
                            AWS_REGION,
                            pStunUrlPostfix );

        if( written < 0 )
        {
            LogError( ( "snprintf fail, errno: %s", strerror( errno ) ) );
            skipProcess = -1;
        }
        else if( written == ICE_CONTROLLER_ICE_SERVER_URL_MAX_LENGTH )
        {
            LogError( ( "buffer has no space for default STUN server" ) );
            skipProcess = -1;
        }
        else
        {
            /* STUN server is written correctly. Set UDP as protocol since we always use UDP to query server reflexive address. */
            pOutputIceServers[ currentIceServerIndex ].protocol = ICE_SOCKET_PROTOCOL_UDP;
            pOutputIceServers[ currentIceServerIndex ].serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_STUN;
            pOutputIceServers[ currentIceServerIndex ].userNameLength = 0U;
            pOutputIceServers[ currentIceServerIndex ].passwordLength = 0U;
            pOutputIceServers[ currentIceServerIndex ].iceEndpoint.isPointToPoint = 0U;
            pOutputIceServers[ currentIceServerIndex ].iceEndpoint.transportAddress.port = 443;
            pOutputIceServers[ currentIceServerIndex ].url[ written ] = '\0'; /* It must be NULL terminated for DNS query. */
            pOutputIceServers[ currentIceServerIndex ].urlLength = written;
            currentIceServerIndex++;
        }
    }

    if( skipProcess == 0 )
    {
        /* Parse Ice server confgis into IceControllerIceServer_t structure. */
        for( i = 0; i < iceServerConfigsCount; i++ )
        {
            if( pIceServerConfigs[ i ].userNameLength > ICE_CONTROLLER_ICE_SERVER_USERNAME_MAX_LENGTH )
            {
                LogError( ( "The length of Ice server's username is too long to store, length: %lu", pIceServerConfigs[ i ].userNameLength ) );
                continue;
            }
            else if( pIceServerConfigs[ i ].passwordLength > ICE_CONTROLLER_ICE_SERVER_PASSWORD_MAX_LENGTH )
            {
                LogError( ( "The length of Ice server's password is too long to store, length: %lu", pIceServerConfigs[ i ].passwordLength ) );
                continue;
            }
            else if( currentIceServerIndex >= *pOutputIceServersCount )
            {
                LogWarn( ( "The size of Ice server buffer has no space for more server info, current index: %lu, buffer size: %lu, skipped server config idx: %u",
                           currentIceServerIndex,
                           *pOutputIceServersCount,
                           i ) );
                break;
            }
            else
            {
                /* Do nothing, coverity happy. */
            }

            for( j = 0; j < pIceServerConfigs[ i ].iceServerUriCount; j++ )
            {
                if( currentIceServerIndex >= *pOutputIceServersCount )
                {
                    LogWarn( ( "The size of Ice server buffer has no space for more server info, current index: %lu, buffer size: %lu, skipped server URL: %.*s",
                               currentIceServerIndex,
                               *pOutputIceServersCount,
                               ( int ) pIceServerConfigs[ i ].iceServerUris[ j ].uriLength,
                               pIceServerConfigs[ i ].iceServerUris[ j ].uri ) );
                    break;
                }

                /* Parse each URI */
                parseResult = ParseIceServerUri( &pOutputIceServers[ currentIceServerIndex ],
                                                 pIceServerConfigs[ i ].iceServerUris[ j ].uri,
                                                 pIceServerConfigs[ i ].iceServerUris[ j ].uriLength );
                if( parseResult != 0 )
                {
                    continue;
                }

                memcpy( pOutputIceServers[ currentIceServerIndex ].userName,
                        pIceServerConfigs[ i ].userName,
                        pIceServerConfigs[ i ].userNameLength );
                pOutputIceServers[ currentIceServerIndex ].userNameLength = pIceServerConfigs[ i ].userNameLength;
                memcpy( pOutputIceServers[ currentIceServerIndex ].password,
                        pIceServerConfigs[ i ].password,
                        pIceServerConfigs[ i ].passwordLength );
                pOutputIceServers[ currentIceServerIndex ].passwordLength = pIceServerConfigs[ i ].passwordLength;
                currentIceServerIndex++;
            }
        }
    }

    if( skipProcess == 0 )
    {
        *pOutputIceServersCount = currentIceServerIndex;
    }

    return skipProcess;
}

static int32_t InitializePeerConnectionSession( DemoContext_t * pDemoContext,
                                                DemoPeerConnectionSession_t * pDemoSession )
{
    int32_t ret = 0;
    PeerConnectionResult_t peerConnectionResult;
    PeerConnectionSessionConfiguration_t pcConfig;

    memset( &pcConfig, 0, sizeof( PeerConnectionSessionConfiguration_t ) );
    pcConfig.canTrickleIce = 1U;
    pcConfig.natTraversalConfigBitmap = ICE_CANDIDATE_NAT_TRAVERSAL_CONFIG_ALLOW_ALL;

    peerConnectionResult = PeerConnection_Init( &pDemoSession->peerConnectionSession, &pcConfig );
    if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
    {
        LogWarn( ( "PeerConnection_Init fail, result: %d", peerConnectionResult ) );
        ret = -1;
    }

    return ret;
}

static int32_t StartPeerConnectionSession( DemoContext_t * pDemoContext,
                                           DemoPeerConnectionSession_t * pDemoSession,
                                           const char * pRemoteClientId,
                                           size_t remoteClientIdLength )
{
    int32_t ret = 0;
    PeerConnectionResult_t peerConnectionResult;
    PeerConnectionSessionConfiguration_t pcConfig;
    Transceiver_t * pTransceiver = NULL;

    if( remoteClientIdLength > REMOTE_ID_MAX_LENGTH )
    {
        LogWarn( ( "The remote client ID length(%lu) is too long to store.", remoteClientIdLength ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        memset( &pcConfig, 0, sizeof( PeerConnectionSessionConfiguration_t ) );
        pcConfig.iceServersCount = ICE_CONTROLLER_MAX_ICE_SERVER_COUNT;
        #if defined( AWS_CA_CERT_PATH )
            pcConfig.pRootCaPath = AWS_CA_CERT_PATH;
            pcConfig.rootCaPathLength = strlen( AWS_CA_CERT_PATH );
        #endif /* #if defined( AWS_CA_CERT_PATH ) */

        #if defined( AWS_CA_CERT_PEM )
            pcConfig.rootCaPem = AWS_CA_CERT_PEM;
            pcConfig.rootCaPemLength = sizeof( AWS_CA_CERT_PEM );
        #endif /* #if defined( AWS_CA_CERT_PEM ) */

        pcConfig.canTrickleIce = 1U;
        pcConfig.natTraversalConfigBitmap = ICE_CANDIDATE_NAT_TRAVERSAL_CONFIG_ALLOW_ALL;

        ret = GetIceServerList( pDemoContext,
                                pcConfig.iceServers,
                                &pcConfig.iceServersCount );
    }

    if( ret == 0 )
    {
        peerConnectionResult = PeerConnection_AddIceServerConfig( &pDemoSession->peerConnectionSession,
                                                                  &pcConfig );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_AddIceServerConfig fail, result: %d", peerConnectionResult ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        peerConnectionResult = PeerConnection_SetOnLocalCandidateReady( &pDemoSession->peerConnectionSession,
                                                                        HandleLocalCandidateReady,
                                                                        pDemoSession );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_SetOnLocalCandidateReady fail, result: %d", peerConnectionResult ) );
            ret = -1;
        }
    }

    /* Add video transceiver */
    if( ret == 0 )
    {
        pTransceiver = &pDemoSession->transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO ];
        ret = AppMediaSource_InitVideoTransceiver( &pDemoContext->appMediaSourcesContext, pTransceiver );
        if( ret != 0 )
        {
            LogError( ( "Fail to get video transceiver." ) );
        }
        else
        {
            peerConnectionResult = PeerConnection_AddTransceiver( &pDemoSession->peerConnectionSession, pTransceiver );
            if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
            {
                LogError( ( "Fail to add video transceiver, result = %d.", peerConnectionResult ) );
                ret = -1;
            }
        }
    }

    /* Add audio transceiver */
    if( ret == 0 )
    {
        pTransceiver = &pDemoSession->transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO ];
        ret = AppMediaSource_InitAudioTransceiver( &pDemoContext->appMediaSourcesContext, pTransceiver );
        if( ret != 0 )
        {
            LogError( ( "Fail to get audio transceiver." ) );
        }
        else
        {
            peerConnectionResult = PeerConnection_AddTransceiver( &pDemoSession->peerConnectionSession, pTransceiver );
            if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
            {
                LogError( ( "Fail to add audio transceiver, result = %d.", peerConnectionResult ) );
                ret = -1;
            }
        }
    }

    if( ret == 0 )
    {
        pDemoSession->remoteClientIdLength = remoteClientIdLength;
        memcpy( pDemoSession->remoteClientId, pRemoteClientId, remoteClientIdLength );
        peerConnectionResult = PeerConnection_Start( &pDemoSession->peerConnectionSession );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogError( ( "Fail to start peer connection, result = %d.", peerConnectionResult ) );
            ret = -1;
        }
    }

    return ret;
}

static DemoPeerConnectionSession_t * GetCreatePeerConnectionSession( DemoContext_t * pDemoContext,
                                                                     const char * pRemoteClientId,
                                                                     size_t remoteClientIdLength,
                                                                     uint8_t allowCreate )
{
    DemoPeerConnectionSession_t * pRet = NULL;
    int i;
    int32_t initResult;

    for( i = 0; i < AWS_MAX_VIEWER_NUM; i++ )
    {
        if( ( pDemoContext->peerConnectionSessions[i].remoteClientIdLength == remoteClientIdLength ) &&
            ( strncmp( pDemoContext->peerConnectionSessions[i].remoteClientId, pRemoteClientId, remoteClientIdLength ) == 0 ) )
        {
            /* Found existing session. */
            pRet = &pDemoContext->peerConnectionSessions[i];
            break;
        }
        else if( ( allowCreate != 0 ) &&
                 ( pRet == NULL ) &&
                 ( pDemoContext->peerConnectionSessions[i].peerConnectionSession.state == PEER_CONNECTION_SESSION_STATE_INITED ) )
        {
            /* Found free session, keep looping to find existing one. */
            pRet = &pDemoContext->peerConnectionSessions[i];
        }
        else
        {
            /* Do nothing. */
        }
    }

    if( ( pRet != NULL ) && ( pRet->peerConnectionSession.state == PEER_CONNECTION_SESSION_STATE_INITED ) )
    {
        /* Initialize Peer Connection. */
        LogDebug( ( "Start peer connection on idx: %d for client ID(%lu): %.*s",
                    i,
                    remoteClientIdLength,
                    ( int ) remoteClientIdLength,
                    pRemoteClientId ) );
        initResult = StartPeerConnectionSession( pDemoContext, pRet, pRemoteClientId, remoteClientIdLength );
        if( initResult != 0 )
        {
            pRet = NULL;
        }
    }

    return pRet;
}

static PeerConnectionResult_t HandleRxVideoFrame( void * pCustomContext,
                                                  PeerConnectionFrame_t * pFrame )
{
    #ifdef ENABLE_STREAMING_LOOPBACK
    webrtc_frame_t frame;

    if( pFrame != NULL )
    {
        LogDebug( ( "Received video frame with length: %u", pFrame->dataLength ) );

        frame.trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
        frame.pData = pFrame->pData;
        frame.size = pFrame->dataLength;
        frame.freeData = 0U;
        frame.timestampUs = pFrame->presentationUs;
        ( void ) OnMediaSinkHook( pCustomContext,
                                  &frame );
    }

    #else /* ifdef ENABLE_STREAMING_LOOPBACK */
    ( void ) pCustomContext;
    if( pFrame != NULL )
    {
        LogDebug( ( "Received video frame with length: %lu", pFrame->dataLength ) );
    }
    #endif /* ifdef ENABLE_STREAMING_LOOPBACK */

    return PEER_CONNECTION_RESULT_OK;
}

static PeerConnectionResult_t HandleRxAudioFrame( void * pCustomContext,
                                                  PeerConnectionFrame_t * pFrame )
{
    #ifdef ENABLE_STREAMING_LOOPBACK
    webrtc_frame_t frame;

    if( pFrame != NULL )
    {
        LogDebug( ( "Received audio frame with length: %u", pFrame->dataLength ) );

        frame.trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
        frame.pData = pFrame->pData;
        frame.size = pFrame->dataLength;
        frame.freeData = 0U;
        frame.timestampUs = pFrame->presentationUs;
        ( void ) OnMediaSinkHook( pCustomContext,
                                  &frame );
    }

    #else /* ifdef ENABLE_STREAMING_LOOPBACK */
    ( void ) pCustomContext;
    if( pFrame != NULL )
    {
        LogDebug( ( "Received audio frame with length: %lu", pFrame->dataLength ) );
    }
    #endif /* ifdef ENABLE_STREAMING_LOOPBACK */

    return PEER_CONNECTION_RESULT_OK;
}

static void HandleSdpOffer( DemoContext_t * pDemoContext,
                            const SignalingMessage_t * pSignalingMessage )
{
    uint8_t skipProcess = 0;
    SignalingControllerResult_t signalingControllerReturn;
    const char * pSdpOfferMessage = NULL;
    size_t sdpOfferMessageLength = 0;
    PeerConnectionResult_t peerConnectionResult;
    PeerConnectionBufferSessionDescription_t bufferSessionDescription;
    size_t formalSdpMessageLength = 0;
    size_t sdpAnswerMessageLength = 0;
    DemoPeerConnectionSession_t * pPcSession = NULL;
    SignalingMessage_t signalingMessageSdpAnswer;

    if( ( pDemoContext == NULL ) ||
        ( pSignalingMessage == NULL ) )
    {
        LogError( ( "Invalid input, pDemoContext: %p, pEvent: %p", pDemoContext, pSignalingMessage ) );
        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        /* Get the SDP content in pSdpOfferMessage. */
        signalingControllerReturn = SignalingController_ExtractSdpOfferFromSignalingMessage( pSignalingMessage->pMessage,
                                                                                             pSignalingMessage->messageLength,
                                                                                             &pSdpOfferMessage,
                                                                                             &sdpOfferMessageLength );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to parse SDP offer content, result: %d, event message(%lu): %.*s.",
                        signalingControllerReturn,
                        pSignalingMessage->messageLength,
                        ( int ) pSignalingMessage->messageLength,
                        pSignalingMessage->pMessage ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        /* Translate the newline into SDP formal format. The end pattern from signaling event message is "\\n" or "\\r\\n",
         * so we replace that with "\n" by calling this function. Note that this doesn't support inplace replacement. */
        formalSdpMessageLength = PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH;
        signalingControllerReturn = SignalingController_DeserializeSdpContentNewline( pSdpOfferMessage,
                                                                                      sdpOfferMessageLength,
                                                                                      pDemoContext->sdpBuffer,
                                                                                      &formalSdpMessageLength );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to deserialize SDP offer newline, result: %d, event message(%lu): %.*s.",
                        signalingControllerReturn,
                        pSignalingMessage->messageLength,
                        ( int ) pSignalingMessage->messageLength,
                        pSignalingMessage->pMessage ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        pPcSession = GetCreatePeerConnectionSession( pDemoContext, pSignalingMessage->pRemoteClientId, pSignalingMessage->remoteClientIdLength, 1U );
        if( pPcSession == NULL )
        {
            LogWarn( ( "No available peer connection session for remote client ID(%lu): %.*s",
                       pSignalingMessage->remoteClientIdLength,
                       ( int ) pSignalingMessage->remoteClientIdLength,
                       pSignalingMessage->pRemoteClientId ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        bufferSessionDescription.pSdpBuffer = pDemoContext->sdpBuffer;
        bufferSessionDescription.sdpBufferLength = formalSdpMessageLength;
        bufferSessionDescription.type = SDP_CONTROLLER_MESSAGE_TYPE_OFFER;
        peerConnectionResult = PeerConnection_SetRemoteDescription( &pPcSession->peerConnectionSession,
                                                                    &bufferSessionDescription );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_AddRemoteCandidate fail, result: %d, dropping ICE candidate.", peerConnectionResult ) );
        }
    }

    if( skipProcess == 0 )
    {
        peerConnectionResult = PeerConnection_SetVideoOnFrame( &pPcSession->peerConnectionSession,
                                                               HandleRxVideoFrame,
                                                               pDemoContext );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_SetVideoOnFrame fail, result: %d.", peerConnectionResult ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        peerConnectionResult = PeerConnection_SetAudioOnFrame( &pPcSession->peerConnectionSession,
                                                               HandleRxAudioFrame,
                                                               pDemoContext );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_SetAudioOnFrame fail, result: %d.", peerConnectionResult ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        memset( &bufferSessionDescription, 0, sizeof( PeerConnectionBufferSessionDescription_t ) );
        bufferSessionDescription.pSdpBuffer = pDemoContext->sdpBuffer;
        bufferSessionDescription.sdpBufferLength = PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH;
        peerConnectionResult = PeerConnection_SetLocalDescription( &pPcSession->peerConnectionSession,
                                                                   &bufferSessionDescription );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_SetLocalDescription fail, result: %d.", peerConnectionResult ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        pDemoContext->sdpConstructedBufferLength = PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH;
        peerConnectionResult = PeerConnection_CreateAnswer( &pPcSession->peerConnectionSession,
                                                            &bufferSessionDescription,
                                                            pDemoContext->sdpConstructedBuffer,
                                                            &pDemoContext->sdpConstructedBufferLength );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_CreateAnswer fail, result: %d.", peerConnectionResult ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        /* Translate from SDP formal format into signaling event message by replacing newline with "\\n" or "\\r\\n". */
        sdpAnswerMessageLength = PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH;
        signalingControllerReturn = SignalingController_SerializeSdpContentNewline( pDemoContext->sdpConstructedBuffer,
                                                                                    pDemoContext->sdpConstructedBufferLength,
                                                                                    pDemoContext->sdpBuffer,
                                                                                    &sdpAnswerMessageLength );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to deserialize SDP offer newline, result: %d, constructed buffer(%lu): %.*s",
                        signalingControllerReturn,
                        pDemoContext->sdpConstructedBufferLength,
                        ( int ) pDemoContext->sdpConstructedBufferLength,
                        pDemoContext->sdpConstructedBuffer ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        signalingMessageSdpAnswer.correlationIdLength = 0U;
        signalingMessageSdpAnswer.pCorrelationId = NULL;
        signalingMessageSdpAnswer.messageType = SIGNALING_TYPE_MESSAGE_SDP_ANSWER;
        signalingMessageSdpAnswer.pMessage = pDemoContext->sdpBuffer;
        signalingMessageSdpAnswer.messageLength = sdpAnswerMessageLength;
        signalingMessageSdpAnswer.pRemoteClientId = pSignalingMessage->pRemoteClientId;
        signalingMessageSdpAnswer.remoteClientIdLength = pSignalingMessage->remoteClientIdLength;

        signalingControllerReturn = SignalingController_SendMessage( &demoContext.signalingControllerContext, &signalingMessageSdpAnswer );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            skipProcess = 1;
            LogError( ( "Send signaling message fail, result: %d", signalingControllerReturn ) );
        }
    }
}

static void HandleRemoteCandidate( DemoContext_t * pDemoContext,
                                   const SignalingMessage_t * pSignalingMessage )
{
    uint8_t skipProcess = 0;
    PeerConnectionResult_t peerConnectionResult;
    DemoPeerConnectionSession_t * pPcSession = NULL;

    pPcSession = GetCreatePeerConnectionSession( pDemoContext, pSignalingMessage->pRemoteClientId, pSignalingMessage->remoteClientIdLength, 1U );
    if( pPcSession == NULL )
    {
        LogWarn( ( "No available peer connection session for remote client ID(%lu): %.*s",
                   pSignalingMessage->remoteClientIdLength,
                   ( int ) pSignalingMessage->remoteClientIdLength,
                   pSignalingMessage->pRemoteClientId ) );
        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        peerConnectionResult = PeerConnection_AddRemoteCandidate( &pPcSession->peerConnectionSession,
                                                                  pSignalingMessage->pMessage,
                                                                  pSignalingMessage->messageLength );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_AddRemoteCandidate fail, result: %d, dropping ICE candidate.", peerConnectionResult ) );
        }
    }
}

static void HandleIceServerReconnect( DemoContext_t * pDemoContext,
                                      const SignalingMessage_t * pSignalingMessage )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    uint64_t initTimeSec = time( NULL );
    uint64_t currTimeSec = initTimeSec;

    while( currTimeSec < initTimeSec + SIGNALING_CONNECT_STATE_TIMEOUT_SEC )
    {
        ret = SignalingController_RefreshIceServerConfigs( &demoContext.signalingControllerContext );

        if( ret == SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogInfo( ( "Ice-Server Reconnection Successful." ) );
            break;
        }
        else
        {
            LogError( ( "Unable to Reconnect Ice Server." ) );

            currTimeSec = time( NULL );
        }
    }
}

static const char * GetCandidateTypeString( IceCandidateType_t candidateType )
{
    const char * ret;

    switch( candidateType )
    {
        case ICE_CANDIDATE_TYPE_HOST:
            ret = DEMO_CANDIDATE_TYPE_HOST_STRING;
            break;
        case ICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
            ret = DEMO_CANDIDATE_TYPE_PRFLX_STRING;
            break;
        case ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
            ret = DEMO_CANDIDATE_TYPE_SRFLX_STRING;
            break;
        case ICE_CANDIDATE_TYPE_RELAY:
            ret = DEMO_CANDIDATE_TYPE_RELAY_STRING;
            break;
        default:
            ret = DEMO_CANDIDATE_TYPE_UNKNOWN_STRING;
            break;
    }

    return ret;
}

static void HandleLocalCandidateReady( void * pCustomContext,
                                       PeerConnectionIceLocalCandidate_t * pIceLocalCandidate )
{
    uint8_t skipProcess = 0;
    DemoPeerConnectionSession_t * pPcSession = ( DemoPeerConnectionSession_t * )pCustomContext;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingMessage_t signalingMessage;
    int written;
    char buffer[ DEMO_JSON_CANDIDATE_MAX_LENGTH ];
    char candidateStringBuffer[ DEMO_JSON_CANDIDATE_MAX_LENGTH ];

    if( ( pPcSession == NULL ) ||
        ( pIceLocalCandidate == NULL ) )
    {
        /* Log for debugging. */
        LogWarn( ( "Invalid local candidate ready event, pPcSession: %p, pIceLocalCandidate: %p",
                   pPcSession,
                   pIceLocalCandidate ) );

        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        if( pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.family == STUN_ADDRESS_IPv4 )
        {
            written = snprintf( candidateStringBuffer, DEMO_JSON_CANDIDATE_MAX_LENGTH, DEMO_ICE_CANDIDATE_JSON_IPV4_TEMPLATE,
                                pIceLocalCandidate->localCandidateIndex,
                                pIceLocalCandidate->pLocalCandidate->priority,
                                pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[0], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[1], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[2], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[3],
                                pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.port,
                                GetCandidateTypeString( pIceLocalCandidate->pLocalCandidate->candidateType ) );
        }
        else
        {
            written = snprintf( candidateStringBuffer, DEMO_JSON_CANDIDATE_MAX_LENGTH, DEMO_ICE_CANDIDATE_JSON_IPV6_TEMPLATE,
                                pIceLocalCandidate->localCandidateIndex,
                                pIceLocalCandidate->pLocalCandidate->priority,
                                pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[0], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[1], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[2], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[3],
                                pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[4], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[5], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[6], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[7],
                                pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[8], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[9], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[10], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[11],
                                pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[12], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[13], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[14], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[15],
                                pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.port,
                                GetCandidateTypeString( pIceLocalCandidate->pLocalCandidate->candidateType ) );
        }

        if( written < 0 )
        {
            LogError( ( "snprintf returns fail, error: %d", written ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        /* Format this into candidate string. */
        memset( &( buffer[ 0 ] ), 0, DEMO_JSON_CANDIDATE_MAX_LENGTH );

        written = snprintf( &( buffer[ 0 ] ), DEMO_JSON_CANDIDATE_MAX_LENGTH, DEMO_ICE_CANDIDATE_JSON_TEMPLATE,
                            written, candidateStringBuffer );

        if( written < 0 )
        {
            LogError( ( "snprintf returns fail, error: %d", written ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        signalingMessage.correlationIdLength = 0U;
        signalingMessage.pCorrelationId = NULL;
        signalingMessage.messageType = SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE;
        signalingMessage.pMessage = &( buffer[ 0 ] );
        signalingMessage.messageLength = written;
        signalingMessage.pRemoteClientId = pPcSession->remoteClientId;
        signalingMessage.remoteClientIdLength = pPcSession->remoteClientIdLength;

        signalingControllerReturn = SignalingController_SendMessage( &demoContext.signalingControllerContext, &signalingMessage );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Send signaling message fail, result: %d", signalingControllerReturn ) );
            skipProcess = 1;
        }
        else
        {
            LogDebug( ( "Sent local candidate to remote peer, msg(%d): %.*s",
                        written,
                        written,
                        &( buffer[ 0 ] ) ) );
        }
    }
}

static int OnSignalingMessageReceived( SignalingMessage_t * pSignalingMessage,
                                       void * pUserData )
{
    ( void ) pUserData;

    LogDebug( ( "Received Message from websocket server!" ) );
    LogDebug( ( "Message Type: %x", pSignalingMessage->messageType ) );
    LogDebug( ( "Sender ID: %.*s", ( int ) pSignalingMessage->remoteClientIdLength, pSignalingMessage->pRemoteClientId ) );
    LogDebug( ( "Correlation ID: %.*s", ( int ) pSignalingMessage->correlationIdLength, pSignalingMessage->pCorrelationId ) );
    LogDebug( ( "Message Length: %lu, Message:", pSignalingMessage->messageLength ) );
    LogDebug( ( "%.*s", ( int ) pSignalingMessage->messageLength, pSignalingMessage->pMessage ) );

    switch( pSignalingMessage->messageType )
    {
        case SIGNALING_TYPE_MESSAGE_SDP_OFFER:
            Metric_StartEvent( METRIC_EVENT_SENDING_FIRST_FRAME );
            HandleSdpOffer( &demoContext, pSignalingMessage );
            break;
        case SIGNALING_TYPE_MESSAGE_SDP_ANSWER:
            break;
        case SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE:
            HandleRemoteCandidate( &demoContext, pSignalingMessage );
            break;
        case SIGNALING_TYPE_MESSAGE_RECONNECT_ICE_SERVER:
            HandleIceServerReconnect( &demoContext, pSignalingMessage );
            break;
        case SIGNALING_TYPE_MESSAGE_STATUS_RESPONSE:
            break;
        default:
            break;
    }

    return 0;
}

#if ENABLE_SCTP_DATA_CHANNEL

#if ( DATACHANNEL_CUSTOM_CALLBACK_HOOK != 0 )

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
        LogWarn( ( "[VIEWER] [Channel Name: %s ID: %d] >>> DataChannel Binary Message", pDataChannel->ucDataChannelName, ( int ) pDataChannel->channelId ) );
    }
    else {
        LogWarn( ( "[VIEWER] [Channel Name: %s ID: %d] >>> DataChannel String Message: %.*s\n", pDataChannel->ucDataChannelName, ( int ) pDataChannel->channelId, ( int ) pMessageLen, pMessage ) );
        sprintf( ucSendMessage, "Received %ld bytes, ECHO: %.*s", ( long int ) pMessageLen, ( int ) ( pMessageLen > ( OP_BUFFER_SIZE - 128 ) ? ( OP_BUFFER_SIZE - 128 ) : pMessageLen ), pMessage );
        retStatus = PeerConnectionSCTP_DataChannelSend( pDataChannel, 0U, ( uint8_t * ) ucSendMessage, strlen( ucSendMessage ) );
    }

    if( retStatus != PEER_CONNECTION_RESULT_OK )
    {
        LogInfo( ( "[KVS Master] onDataChannelMessage(): operation returned status code: 0x%08x \n", ( unsigned int ) retStatus ) );
    }

}

OnDataChannelMessageReceived_t PeerConnectionSCTP_SetChannelOneMessageCallbackHook( PeerConnectionSession_t * pPeerConnectionSession,
                                                                                    uint32_t ulChannelId,
                                                                                    uint8_t * pucName,
                                                                                    uint32_t ulNameLen )
{
    ( void ) pPeerConnectionSession;
    ( void ) ulChannelId;
    ( void ) pucName;
    ( void ) ulNameLen;

    return onDataChannelMessage;
}

#endif /* (DATACHANNEL_CUSTOM_CALLBACK_HOOK != 0) */

#endif /* ENABLE_SCTP_DATA_CHANNEL */

int main()
{
    int ret = 0;
    int i;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerConnectInfo_t connectInfo;
    SSLCredentials_t sslCreds;

    srand( time( NULL ) );

    srtp_init();

    #if ENABLE_SCTP_DATA_CHANNEL
    SCTP_InitSCTPSession();
    #endif /* ENABLE_SCTP_DATA_CHANNEL */

    memset( &demoContext, 0, sizeof( DemoContext_t ) );
    memset( &sslCreds, 0, sizeof( SSLCredentials_t ) );
    memset( &connectInfo, 0, sizeof( SignalingControllerConnectInfo_t ) );

    sslCreds.pCaCertPath = AWS_CA_CERT_PATH;
    #if defined( AWS_IOT_THING_ROLE_ALIAS )
    sslCreds.pDeviceCertPath = AWS_IOT_THING_CERT_PATH;
    sslCreds.pDeviceKeyPath = AWS_IOT_THING_PRIVATE_KEY_PATH;
    #else
    sslCreds.pDeviceCertPath = NULL;
    sslCreds.pDeviceKeyPath = NULL;
    #endif

    connectInfo.awsConfig.pRegion = AWS_REGION;
    connectInfo.awsConfig.regionLen = strlen( AWS_REGION );
    connectInfo.awsConfig.pService = "kinesisvideo";
    connectInfo.awsConfig.serviceLen = strlen( "kinesisvideo" );

    connectInfo.channelName.pChannelName = AWS_KVS_CHANNEL_NAME;
    connectInfo.channelName.channelNameLength = strlen( AWS_KVS_CHANNEL_NAME );

    connectInfo.pUserAgentName = AWS_KVS_AGENT_NAME;
    connectInfo.userAgentNameLength = strlen( AWS_KVS_AGENT_NAME );

    connectInfo.messageReceivedCallback = OnSignalingMessageReceived;
    connectInfo.pMessageReceivedCallbackData = NULL;

    #if defined( AWS_ACCESS_KEY_ID )
    connectInfo.awsCreds.pAccessKeyId = AWS_ACCESS_KEY_ID;
    connectInfo.awsCreds.accessKeyIdLen = strlen( AWS_ACCESS_KEY_ID );
    connectInfo.awsCreds.pSecretAccessKey = AWS_SECRET_ACCESS_KEY;
    connectInfo.awsCreds.secretAccessKeyLen = strlen( AWS_SECRET_ACCESS_KEY );
    #if defined( AWS_SESSION_TOKEN )
    connectInfo.awsCreds.pSessionToken = AWS_SESSION_TOKEN;
    connectInfo.awsCreds.sessionTokenLength = strlen( AWS_SESSION_TOKEN );
    #endif     /* #if defined( AWS_SESSION_TOKEN ) */
    #endif /* #if defined( AWS_ACCESS_KEY_ID ) */

    #if defined( AWS_IOT_THING_ROLE_ALIAS )
    connectInfo.awsIotCreds.pIotCredentialsEndpoint = AWS_CREDENTIALS_ENDPOINT;
    connectInfo.awsIotCreds.iotCredentialsEndpointLength = strlen( AWS_CREDENTIALS_ENDPOINT );
    connectInfo.awsIotCreds.pThingName = AWS_IOT_THING_NAME;
    connectInfo.awsIotCreds.thingNameLength = strlen( AWS_IOT_THING_NAME );
    connectInfo.awsIotCreds.pRoleAlias = AWS_IOT_THING_ROLE_ALIAS;
    connectInfo.awsIotCreds.roleAliasLength = strlen( AWS_IOT_THING_ROLE_ALIAS );
    #endif /* #if defined( AWS_IOT_THING_ROLE_ALIAS ) */

    signalingControllerReturn = SignalingController_Init( &demoContext.signalingControllerContext, &sslCreds );

    if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
    {
        LogError( ( "Fail to initialize signaling controller." ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        /* Set the signal handler to release resource correctly. */
        signal( SIGINT, terminateHandler );

        /* Initialize metrics. */
        Metric_Init();
    }

    if( ret == 0 )
    {
        ret = InitializeAppMediaSource( &demoContext );
    }

    if( ret == 0 )
    {
        for( i = 0; i < AWS_MAX_VIEWER_NUM; i++ )
        {
            ret = InitializePeerConnectionSession( &demoContext,
                                                   &demoContext.peerConnectionSessions[i] );
            if( ret != 0 )
            {
                LogError( ( "Fail to initialize peer connection sessions." ) );
                break;
            }
        }
    }

    if( ret == 0 )
    {
        /* This should never return unless exception happens. */
        signalingControllerReturn = SignalingController_StartListening( &demoContext.signalingControllerContext,
                                                                        &connectInfo );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to keep processing signaling controller." ) );
            ret = -1;
        }
    }

    #if ENABLE_SCTP_DATA_CHANNEL
    /* TODO_SCTP: Move to a common shutdown function? */
    SCTP_DeInitSCTPSession();
    #endif /* ENABLE_SCTP_DATA_CHANNEL */

    return 0;
}
