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
#define DEMO_ICE_CANDIDATE_JSON_IPV6_TEMPLATE "candidate:%lu 1 udp %u %02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X "\
    "%d typ %s raddr ::/0 rport 0 generation 0 network-cost 999"


#define ICE_SERVER_TYPE_STUN "stun:"
#define ICE_SERVER_TYPE_STUN_LENGTH ( 5 )
#define ICE_SERVER_TYPE_TURN "turn:"
#define ICE_SERVER_TYPE_TURN_LENGTH ( 5 )
#define ICE_SERVER_TYPE_TURNS "turns:"
#define ICE_SERVER_TYPE_TURNS_LENGTH ( 6 )

DemoContext_t demoContext;

static int32_t handleSignalingMessage( SignalingControllerReceiveEvent_t * pEvent,
                                       void * pUserContext );
static int32_t InitializePeerConnectionSession( DemoContext_t * pDemoContext,
                                                DemoPeerConnectionSession_t * pSession,
                                                const char * pRemoteClientId,
                                                size_t remoteClientIdLength );

static DemoPeerConnectionSession_t * GetCreatePeerConnectionSession( DemoContext_t * pDemoContext,
                                                                     const char * pRemoteClientId,
                                                                     size_t remoteClientIdLength,
                                                                     uint8_t allowCreate );
static void HandleRemoteCandidate( DemoContext_t * pDemoContext,
                                   const SignalingControllerReceiveEvent_t * pEvent );
static void HandleIceServerReconnect( DemoContext_t * pDemoContext,
                                      const SignalingControllerReceiveEvent_t * pEvent );
static void HandleLocalCandidateReady( void * pCustomContext,
                                       PeerConnectionIceLocalCandidate_t * pIceLocalCandidate );
static void HandleSdpOffer( DemoContext_t * pDemoContext,
                            const SignalingControllerReceiveEvent_t * pEvent );
static const char * GetCandidateTypeString( IceCandidateType_t candidateType );
static int32_t OnSendIceCandidateComplete( SignalingControllerEventStatus_t status,
                                           void * pUserContext );


static void terminateHandler( int sig )
{
    SignalingController_Deinit( &demoContext.signalingControllerContext );
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
        if( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO )
        {
            ret = AppMediaSource_GetVideoTransceiver( &pDemoContext->appMediaSourcesContext, &pTransceiver );
        }
        else if( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_AUDIO )
        {
            ret = AppMediaSource_GetAudioTransceiver( &pDemoContext->appMediaSourcesContext, &pTransceiver );
        }
        else
        {
            LogError( ( "Unknown track kind: %d", pFrame->trackKind ) );
            ret = -2;
        }
    }

    if( ret == 0 )
    {
        peerConnectionFrame.version = PEER_CONNECTION_FRAME_CURRENT_VERSION;
        peerConnectionFrame.presentationUs = pFrame->timestampUs;
        peerConnectionFrame.pData = pFrame->pData;
        peerConnectionFrame.dataLength = pFrame->size;

        for( i = 0; i < AWS_MAX_VIEWER_NUM; i++ )
        {
            if( pDemoContext->peerConnectionSessions[i].isInUse != 0 )
            {
                peerConnectionResult = PeerConnection_WriteFrame( &pDemoContext->peerConnectionSessions[ i ].peerConnectionSession,
                                                                  pTransceiver,
                                                                  &peerConnectionFrame );
                if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
                {
                    LogError( ( "Fail to write frame, result: %d", peerConnectionResult ) );
                    ret = -3;
                    break;
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
        ret = AppMediaSource_Init( &pDemoContext->appMediaSourcesContext, OnMediaSinkHook, pDemoContext );
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
        pIceServer->serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_TURN;
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
        if( ( pIceServer->serverType == ICE_CONTROLLER_ICE_SERVER_TYPE_TURN ) && ( pCurr >= pTail ) )
        {
            LogWarn( ( "No valid transport string found" ) );
            ret = -1;
        }
        else if( pIceServer->serverType == ICE_CONTROLLER_ICE_SERVER_TYPE_TURN )
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
    SignalingControllerIceServerConfig_t * pIceServerConfigs;
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
                LogWarn( ( "The size of Ice server buffer has no space for more server info, current index: %lu, buffer size: %lu",
                           currentIceServerIndex,
                           *pOutputIceServersCount ) );
                break;
            }
            else
            {
                /* Do nothing, coverity happy. */
            }

            for( j = 0; j < pIceServerConfigs[ i ].uriCount; j++ )
            {
                /* Parse each URI */
                parseResult = ParseIceServerUri( &pOutputIceServers[ currentIceServerIndex ],
                                                 pIceServerConfigs[ i ].uris[ j ],
                                                 pIceServerConfigs[ i ].urisLength[ j ] );
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

                if( currentIceServerIndex >= *pOutputIceServersCount )
                {
                    LogWarn( ( "The size of Ice server buffer has no space for more server info, current index: %lu, buffer size: %lu",
                               currentIceServerIndex,
                               *pOutputIceServersCount ) );
                    break;
                }
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
                                                DemoPeerConnectionSession_t * pSession,
                                                const char * pRemoteClientId,
                                                size_t remoteClientIdLength )
{
    int32_t ret = 0;
    PeerConnectionResult_t peerConnectionResult;
    PeerConnectionSessionConfiguration_t pcConfig;
    Transceiver_t * pTransceiver;

    if( remoteClientIdLength > SIGNALING_CONTROLLER_REMOTE_ID_MAX_LENGTH )
    {
        LogWarn( ( "The remote client ID length(%lu) is too long to store.", remoteClientIdLength ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        memset( &pcConfig, 0, sizeof( PeerConnectionSessionConfiguration_t ) );
        pcConfig.canTrickleIce = 1U;
        pcConfig.iceServersCount = ICE_CONTROLLER_MAX_ICE_SERVER_COUNT;

        ret = GetIceServerList( pDemoContext,
                                pcConfig.iceServers,
                                &pcConfig.iceServersCount );
    }

    if( ret == 0 )
    {
        peerConnectionResult = PeerConnection_Init( &pSession->peerConnectionSession,
                                                    &pcConfig );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_Init fail, result: %d", peerConnectionResult ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        peerConnectionResult = PeerConnection_SetOnLocalCandidateReady( &pSession->peerConnectionSession,
                                                                        HandleLocalCandidateReady,
                                                                        pSession );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_SetOnLocalCandidateReady fail, result: %d", peerConnectionResult ) );
            ret = -1;
        }
    }

    /* Add video transceiver */
    if( ret == 0 )
    {
        ret = AppMediaSource_GetVideoTransceiver( &pDemoContext->appMediaSourcesContext, &pTransceiver );
        if( ret != 0 )
        {
            LogError( ( "Fail to get video transceiver." ) );
        }
        else
        {
            peerConnectionResult = PeerConnection_AddTransceiver( &pSession->peerConnectionSession, pTransceiver );
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
        ret = AppMediaSource_GetAudioTransceiver( &pDemoContext->appMediaSourcesContext, &pTransceiver );
        if( ret != 0 )
        {
            LogError( ( "Fail to get audio transceiver." ) );
        }
        else
        {
            peerConnectionResult = PeerConnection_AddTransceiver( &pSession->peerConnectionSession, pTransceiver );
            if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
            {
                LogError( ( "Fail to add audio transceiver, result = %d.", peerConnectionResult ) );
                ret = -1;
            }
        }
    }

    if( ret == 0 )
    {
        pSession->isInUse = 1U;
        pSession->remoteClientIdLength = remoteClientIdLength;
        memcpy( pSession->remoteClientId, pRemoteClientId, remoteClientIdLength );
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
                 ( pDemoContext->peerConnectionSessions[i].isInUse == 0 ) )
        {
            /* Found free session, keep looping to find existing one. */
            pRet = &pDemoContext->peerConnectionSessions[i];
        }
        else
        {
            /* Do nothing. */
        }
    }

    if( ( pRet != NULL ) && ( pRet->isInUse == 0 ) )
    {
        /* Initialize Peer Connection. */
        LogDebug( ( "Initialize peer connection on idx: %d for client ID(%lu): %.*s",
                    i,
                    remoteClientIdLength,
                    ( int ) remoteClientIdLength,
                    pRemoteClientId ) );
        initResult = InitializePeerConnectionSession( pDemoContext, pRet, pRemoteClientId, remoteClientIdLength );
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
                            const SignalingControllerReceiveEvent_t * pEvent )
{
    uint8_t skipProcess = 0;
    SignalingControllerResult_t signalingControllerReturn;
    const char * pSdpOfferMessage = NULL;
    size_t sdpOfferMessageLength = 0;
    PeerConnectionResult_t peerConnectionResult;
    PeerConnectionBufferSessionDescription_t bufferSessionDescription;
    size_t formalSdpMessageLength = 0;
    size_t sdpAnswerMessageLength = 0;
    SignalingControllerEventMessage_t eventMessage = {
        .event = SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE,
        .onCompleteCallback = NULL,
        .pOnCompleteCallbackContext = NULL,
    };
    DemoPeerConnectionSession_t * pPcSession = NULL;

    if( ( pDemoContext == NULL ) ||
        ( pEvent == NULL ) )
    {
        LogError( ( "Invalid input, pDemoContext: %p, pEvent: %p", pDemoContext, pEvent ) );
        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        /* Get the SDP content in pSdpOfferMessage. */
        signalingControllerReturn = SignalingController_GetSdpContentFromEventMsg( pEvent->pDecodeMessage,
                                                                                   pEvent->decodeMessageLength,
                                                                                   1U,
                                                                                   &pSdpOfferMessage,
                                                                                   &sdpOfferMessageLength );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to parse SDP offer content, result: %d, event message(%lu): %.*s.",
                        signalingControllerReturn,
                        pEvent->decodeMessageLength,
                        ( int ) pEvent->decodeMessageLength,
                        pEvent->pDecodeMessage ) );
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
                        pEvent->decodeMessageLength,
                        ( int ) pEvent->decodeMessageLength,
                        pEvent->pDecodeMessage ) );
            skipProcess = 1;
        }
    }

    if( skipProcess == 0 )
    {
        pPcSession = GetCreatePeerConnectionSession( pDemoContext, pEvent->pRemoteClientId, pEvent->remoteClientIdLength, 1U );
        if( pPcSession == NULL )
        {
            LogWarn( ( "No available peer connection session for remote client ID(%lu): %.*s",
                       pEvent->remoteClientIdLength,
                       ( int ) pEvent->remoteClientIdLength,
                       pEvent->pRemoteClientId ) );
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
        eventMessage.eventContent.correlationIdLength = 0U;
        memset( eventMessage.eventContent.correlationId, 0, SIGNALING_CONTROLLER_CORRELATION_ID_MAX_LENGTH );
        eventMessage.eventContent.messageType = SIGNALING_TYPE_MESSAGE_SDP_ANSWER;
        eventMessage.eventContent.pDecodeMessage = pDemoContext->sdpBuffer;
        eventMessage.eventContent.decodeMessageLength = sdpAnswerMessageLength;
        memcpy( eventMessage.eventContent.remoteClientId, pEvent->pRemoteClientId, pEvent->remoteClientIdLength );
        eventMessage.eventContent.remoteClientIdLength = pEvent->remoteClientIdLength;

        signalingControllerReturn = SignalingController_SendMessage( &demoContext.signalingControllerContext, &eventMessage );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            skipProcess = 1;
            LogError( ( "Send signaling message fail, result: %d", signalingControllerReturn ) );
        }
    }
}

static void HandleRemoteCandidate( DemoContext_t * pDemoContext,
                                   const SignalingControllerReceiveEvent_t * pEvent )
{
    uint8_t skipProcess = 0;
    PeerConnectionResult_t peerConnectionResult;
    DemoPeerConnectionSession_t * pPcSession = NULL;

    pPcSession = GetCreatePeerConnectionSession( pDemoContext, pEvent->pRemoteClientId, pEvent->remoteClientIdLength, 1U );
    if( pPcSession == NULL )
    {
        LogWarn( ( "No available peer connection session for remote client ID(%lu): %.*s",
                   pEvent->remoteClientIdLength,
                   ( int ) pEvent->remoteClientIdLength,
                   pEvent->pRemoteClientId ) );
        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        peerConnectionResult = PeerConnection_AddRemoteCandidate( &pPcSession->peerConnectionSession,
                                                                  pEvent->pDecodeMessage, pEvent->decodeMessageLength );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_AddRemoteCandidate fail, result: %d, dropping ICE candidate.", peerConnectionResult ) );
        }
    }
}

static void HandleIceServerReconnect( DemoContext_t * pDemoContext,
                                      const SignalingControllerReceiveEvent_t * pEvent )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    uint64_t initTimeSec = time( NULL );
    uint64_t currTimeSec = initTimeSec;

    while( currTimeSec < initTimeSec + SIGNALING_CONNECT_STATE_TIMEOUT_SEC )
    {
        ret = SignalingController_IceServerReconnection( &demoContext.signalingControllerContext );

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

static int32_t OnSendIceCandidateComplete( SignalingControllerEventStatus_t status,
                                           void * pUserContext )
{
    LogDebug( ( "Freeing buffer at %p", pUserContext ) );
    free( pUserContext );

    return 0;
}

static void HandleLocalCandidateReady( void * pCustomContext,
                                       PeerConnectionIceLocalCandidate_t * pIceLocalCandidate )
{
    uint8_t skipProcess = 0;
    DemoPeerConnectionSession_t * pPcSession = ( DemoPeerConnectionSession_t * )pCustomContext;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerEventMessage_t eventMessage = {
        .event = SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE,
        .onCompleteCallback = OnSendIceCandidateComplete,
        .pOnCompleteCallbackContext = NULL,
    };
    int written;
    char * pBuffer;
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
        pBuffer = ( char * ) malloc( DEMO_JSON_CANDIDATE_MAX_LENGTH );
        LogVerbose( ( "Allocating buffer at %p", pBuffer ) );
        memset( pBuffer, 0, DEMO_JSON_CANDIDATE_MAX_LENGTH );

        written = snprintf( pBuffer, DEMO_JSON_CANDIDATE_MAX_LENGTH, DEMO_ICE_CANDIDATE_JSON_TEMPLATE,
                            written, candidateStringBuffer );

        if( written < 0 )
        {
            LogError( ( "snprintf returns fail, error: %d", written ) );
            skipProcess = 1;
            free( pBuffer );
        }
    }

    if( skipProcess == 0 )
    {
        eventMessage.eventContent.correlationIdLength = 0U;
        eventMessage.eventContent.messageType = SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE;
        eventMessage.eventContent.pDecodeMessage = pBuffer;
        eventMessage.eventContent.decodeMessageLength = written;
        memcpy( eventMessage.eventContent.remoteClientId, pPcSession->remoteClientId, pPcSession->remoteClientIdLength );
        eventMessage.eventContent.remoteClientIdLength = pPcSession->remoteClientIdLength;

        /* We dynamically allocate buffer for signaling controller to keep using it.
         * callback it as context to free memory. */
        eventMessage.pOnCompleteCallbackContext = pBuffer;

        signalingControllerReturn = SignalingController_SendMessage( &demoContext.signalingControllerContext, &eventMessage );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Send signaling message fail, result: %d", signalingControllerReturn ) );
            skipProcess = 1;
            free( pBuffer );
        }
        else
        {
            LogDebug( ( "Sent local candidate to remote peer, msg(%d): %.*s",
                        written,
                        written,
                        pBuffer ) );
        }
    }
}

static int32_t handleSignalingMessage( SignalingControllerReceiveEvent_t * pEvent,
                                       void * pUserContext )
{
    ( void ) pUserContext;

    LogDebug( ( "Received Message from websocket server!" ) );
    LogDebug( ( "Message Type: %x", pEvent->messageType ) );
    LogDebug( ( "Sender ID: %.*s", ( int ) pEvent->remoteClientIdLength, pEvent->pRemoteClientId ) );
    LogDebug( ( "Correlation ID: %.*s", ( int ) pEvent->correlationIdLength, pEvent->pCorrelationId ) );
    LogDebug( ( "Message Length: %lu, Message:", pEvent->decodeMessageLength ) );
    LogDebug( ( "%.*s", ( int ) pEvent->decodeMessageLength, pEvent->pDecodeMessage ) );

    switch( pEvent->messageType )
    {
        case SIGNALING_TYPE_MESSAGE_SDP_OFFER:
            Metric_StartEvent( METRIC_EVENT_SENDING_FIRST_FRAME );
            HandleSdpOffer( &demoContext, pEvent );
            break;
        case SIGNALING_TYPE_MESSAGE_SDP_ANSWER:
            break;
        case SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE:
            HandleRemoteCandidate( &demoContext, pEvent );
            break;
        case SIGNALING_TYPE_MESSAGE_RECONNECT_ICE_SERVER:
            HandleIceServerReconnect( &demoContext, pEvent );
            break;
        case SIGNALING_TYPE_MESSAGE_STATUS_RESPONSE:
            break;
        default:
            break;
    }

    return 0;
}

// void *executeIceController( void *pArgs )
// {
//     IceControllerContext_t *pIceControllerContext = (IceControllerContext_t *) pArgs;
//     IceControllerResult_t iceControllerReturn;

//     while( 1 )
//     {
//         iceControllerReturn = IceController_ProcessLoop( pIceControllerContext );
//     }

//     return 0;
// }

int main()
{
    int ret = 0;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerCredential_t signalingControllerCred;

    srand( time( NULL ) );

    srtp_init();

    memset( &demoContext, 0, sizeof( DemoContext_t ) );

    memset( &signalingControllerCred, 0, sizeof( SignalingControllerCredential_t ) );
    signalingControllerCred.pRegion = AWS_REGION;
    signalingControllerCred.regionLength = strlen( AWS_REGION );
    signalingControllerCred.pChannelName = AWS_KVS_CHANNEL_NAME;
    signalingControllerCred.channelNameLength = strlen( AWS_KVS_CHANNEL_NAME );
    signalingControllerCred.pUserAgentName = AWS_KVS_AGENT_NAME;
    signalingControllerCred.userAgentNameLength = strlen( AWS_KVS_AGENT_NAME );
    signalingControllerCred.pAccessKeyId = AWS_ACCESS_KEY_ID;
    signalingControllerCred.accessKeyIdLength = strlen( AWS_ACCESS_KEY_ID );
    signalingControllerCred.pSecretAccessKey = AWS_SECRET_ACCESS_KEY;
    signalingControllerCred.secretAccessKeyLength = strlen( AWS_SECRET_ACCESS_KEY );
    signalingControllerCred.pCaCertPath = AWS_CA_CERT_PATH;

    signalingControllerReturn = SignalingController_Init( &demoContext.signalingControllerContext, &signalingControllerCred, handleSignalingMessage, NULL );
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
        signalingControllerReturn = SignalingController_ConnectServers( &demoContext.signalingControllerContext );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to connect with signaling controller." ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        /* This should never return unless exception happens. */
        signalingControllerReturn = SignalingController_ProcessLoop( &demoContext.signalingControllerContext );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to keep processing signaling controller." ) );
            ret = -1;
        }
    }

    return 0;
}
