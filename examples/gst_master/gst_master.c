#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "../demo_config/demo_config.h"
#include "logging.h"
#include "../demo_config/demo_data_types.h"
#include "signaling_controller.h"
#include "sdp_controller.h"
#include "string_utils.h"
#include "metric.h"
#include "networking_utils.h"

#if ENABLE_SCTP_DATA_CHANNEL
#include "peer_connection_sctp.h"
#endif


#define DEMO_CANDIDATE_TYPE_HOST_STRING "host"
#define DEMO_CANDIDATE_TYPE_SRFLX_STRING "srflx"
#define DEMO_CANDIDATE_TYPE_PRFLX_STRING "prflx"
#define DEMO_CANDIDATE_TYPE_RELAY_STRING "relay"
#define DEMO_CANDIDATE_TYPE_UNKNOWN_STRING "unknown"

#define AWS_DEFAULT_STUN_SERVER_URL_POSTFIX "amazonaws.com"
#define AWS_DEFAULT_STUN_SERVER_URL_POSTFIX_CN "amazonaws.com.cn"
#define AWS_DEFAULT_STUN_SERVER_URL "stun.kinesisvideo.%s.%s"

#define IS_USERNAME_FOUND_BIT ( 1 << 0 )
#define IS_PASSWORD_FOUND_BIT ( 1 << 1 )
#define SET_REMOTE_INFO_USERNAME_FOUND( isFoundBit ) ( isFoundBit |= IS_USERNAME_FOUND_BIT )
#define SET_REMOTE_INFO_PASSWORD_FOUND( isFoundBit ) ( isFoundBit |= IS_PASSWORD_FOUND_BIT )
#define IS_REMOTE_INFO_ALL_FOUND( isFoundBit ) ( isFoundBit & IS_USERNAME_FOUND_BIT && isFoundBit & IS_PASSWORD_FOUND_BIT )

#define DEMO_JSON_CANDIDATE_MAX_LENGTH ( 512 )
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

#define EMA_ALPHA_VALUE           ( ( double ) 0.05 )
#define ONE_MINUS_EMA_ALPHA_VALUE ( ( double ) ( 1 - EMA_ALPHA_VALUE ) )
#define EMA_ACCUMULATOR_GET_NEXT( a, v ) ( double )( EMA_ALPHA_VALUE * ( v ) + ONE_MINUS_EMA_ALPHA_VALUE * ( a ) )

#ifndef MIN
#define MIN( a, b ) ( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#endif

#ifndef MAX
#define MAX( a, b ) ( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )
#endif

// Global variables
static GstElement* senderPipeline = NULL;
static volatile int keepRunning = 1;
DemoContext_t demoContext;

// Forward declarations for all handlers
static int OnSignalingMessageReceived(SignalingMessage_t* pSignalingMessage, void* pUserData);
#if ENABLE_TWCC_SUPPORT
static void SampleSenderBandwidthEstimationHandler(void* pCustomContext,
                                                 TwccBandwidthInfo_t* pTwccBandwidthInfo);
#endif
static int32_t InitializePeerConnectionSession(DemoContext_t* pDemoContext,
                                             DemoPeerConnectionSession_t* pDemoSession);
static int32_t StartPeerConnectionSession(DemoContext_t* pDemoContext,
                                        DemoPeerConnectionSession_t* pDemoSession,
                                        const char* pRemoteClientId,
                                        size_t remoteClientIdLength);
static DemoPeerConnectionSession_t* GetCreatePeerConnectionSession(DemoContext_t* pDemoContext,
                                                                const char* pRemoteClientId,
                                                                size_t remoteClientIdLength,
                                                                uint8_t allowCreate);
static void HandleRemoteCandidate(DemoContext_t* pDemoContext,
                               const SignalingMessage_t* pSignalingMessage);
static void HandleIceServerReconnect(DemoContext_t* pDemoContext,
                                  const SignalingMessage_t* pSignalingMessage);
static void HandleLocalCandidateReady(void* pCustomContext,
                                   PeerConnectionIceLocalCandidate_t* pIceLocalCandidate);
static void HandleSdpOffer(DemoContext_t* pDemoContext,
                        const SignalingMessage_t* pSignalingMessage);
static const char* GetCandidateTypeString(IceCandidateType_t candidateType);


static int32_t OnMediaSinkHook(void* pCustom, webrtc_frame_t* pFrame)
{
    int32_t ret = 0;
    DemoContext_t* pDemoContext = (DemoContext_t*)pCustom;
    PeerConnectionResult_t peerConnectionResult;
    Transceiver_t* pTransceiver = NULL;
    PeerConnectionFrame_t peerConnectionFrame;
    int i;

    if ((pDemoContext == NULL) || (pFrame == NULL)) {
        LogError(("Invalid input, pCustom: %p, pFrame: %p", pCustom, pFrame));
        ret = -1;
    }

    if (ret == 0) {
        peerConnectionFrame.version = PEER_CONNECTION_FRAME_CURRENT_VERSION;
        peerConnectionFrame.presentationUs = pFrame->timestampUs;
        peerConnectionFrame.pData = pFrame->pData;
        peerConnectionFrame.dataLength = pFrame->size;

        for (i = 0; i < AWS_MAX_VIEWER_NUM; i++) {
            if (pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO) {
                pTransceiver = &pDemoContext->peerConnectionSessions[i].transceivers[DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO];
            } else if (pFrame->trackKind == TRANSCEIVER_TRACK_KIND_AUDIO) {
                pTransceiver = &pDemoContext->peerConnectionSessions[i].transceivers[DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO];
            } else {
                LogWarn(("Unknown track kind: %d", pFrame->trackKind));
                break;
            }

            if (pDemoContext->peerConnectionSessions[i].peerConnectionSession.state == PEER_CONNECTION_SESSION_STATE_CONNECTION_READY) {
                peerConnectionResult = PeerConnection_WriteFrame(
                    &pDemoContext->peerConnectionSessions[i].peerConnectionSession,
                    pTransceiver,
                    &peerConnectionFrame);

                if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
                    LogError(("Fail to write %s frame, result: %d",
                        (pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO) ? "video" : "audio",
                        peerConnectionResult));
                    ret = -3;
                }
            }
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
initResult = StartPeerConnectionSession( pDemoContext,
pRet,
pRemoteClientId,
remoteClientIdLength );
if( initResult != 0 )
{
pRet = NULL;
}
}

return pRet;
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


static void terminateHandler(int sig)
{
    LogInfo(("Received signal %d, stopping...", sig));
    keepRunning = 0;
    if (senderPipeline != NULL) {
        gst_element_set_state(senderPipeline, GST_STATE_NULL);
    }
    exit(0);
}


// GStreamer specific callbacks
GstFlowReturn on_new_sample(GstElement* sink, gpointer data, int32_t trackIndex)
{
    GstFlowReturn ret = GST_FLOW_OK;
    GstSample* sample = NULL;
    GstBuffer* buffer = NULL;
    GstMapInfo info = {0};
    GstSegment* segment;
    GstClockTime buf_pts;
    webrtc_frame_t frame = {0};
    DemoContext_t* pDemoContext = (DemoContext_t*)data;
    uint8_t mapped = 0;

    if (pDemoContext == NULL) {
        LogError(("NULL demo context"));
        goto CleanUp;
    }

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (sample == NULL) {
        goto CleanUp;
    }

    buffer = gst_sample_get_buffer(sample);
    if (buffer == NULL) {
        goto CleanUp;
    }

    uint8_t isDroppable = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED) ||
                         GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DECODE_ONLY) ||
                         (GST_BUFFER_FLAGS(buffer) == GST_BUFFER_FLAG_DISCONT) ||
                         (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT) &&
                          GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) ||
                         !GST_BUFFER_PTS_IS_VALID(buffer);

    if (!isDroppable) {
        segment = gst_sample_get_segment(sample);
        buf_pts = gst_segment_to_running_time(segment, GST_FORMAT_TIME, buffer->pts);
        if (!GST_CLOCK_TIME_IS_VALID(buf_pts)) {
            LogError(("Frame contains invalid PTS dropping the frame"));
            goto CleanUp;
        }

        if (!gst_buffer_map(buffer, &info, GST_MAP_READ)) {
            LogError(("Gst buffer mapping failed"));
            goto CleanUp;
        }
        mapped = 1;

        frame.trackKind = (trackIndex == DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO) ?
                         TRANSCEIVER_TRACK_KIND_VIDEO : TRANSCEIVER_TRACK_KIND_AUDIO;
        frame.timestampUs = buf_pts / 1000; // Convert from ns to us
        frame.size = info.size;
        frame.pData = info.data;
        frame.freeData = 0;

        OnMediaSinkHook(pDemoContext, &frame);
    }

CleanUp:
    if (mapped) {
        gst_buffer_unmap(buffer, &info);
    }

    if (sample != NULL) {
        gst_sample_unref(sample);
    }

    return ret;
}

GstFlowReturn on_new_sample_video(GstElement* sink, gpointer data)
{
    return on_new_sample(sink, data, DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO);
}

GstFlowReturn on_new_sample_audio(GstElement* sink, gpointer data)
{
    return on_new_sample(sink, data, DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO);
}

void* sendGstreamerAudioVideo(void* args)
{
    int32_t retStatus = 0;
    GstElement *appsinkVideo = NULL, *appsinkAudio = NULL;
    GstBus* bus;
    GstMessage* msg;
    DemoContext_t* pDemoContext = (DemoContext_t*)args;

    if (pDemoContext == NULL) {
        LogError(("NULL demo context"));
        goto CleanUp;
    }

    LogInfo(("Creating GStreamer pipeline"));
    senderPipeline = gst_parse_launch(
        "videotestsrc pattern=ball is-live=TRUE ! "
        "queue ! videorate ! videoscale ! videoconvert ! video/x-raw,width=1280,height=720,framerate=25/1 ! "
        "clockoverlay halignment=right valignment=top time-format=\"%Y-%m-%d %H:%M:%S\" ! "
        "x264enc bframes=0 speed-preset=veryfast bitrate=512 byte-stream=TRUE tune=zerolatency ! "
        "video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline ! "
        "appsink sync=TRUE emit-signals=TRUE name=appsink-video "
        "audiotestsrc wave=ticks is-live=TRUE ! "
        "queue leaky=2 max-size-buffers=400 ! audioconvert ! audioresample ! "
        "opusenc ! audio/x-opus,rate=48000,channels=2 ! "
        "appsink sync=TRUE emit-signals=TRUE name=appsink-audio",
        NULL);

    if (senderPipeline == NULL) {
        LogError(("Failed to create pipeline"));
        goto CleanUp;
    }

    appsinkVideo = gst_bin_get_by_name(GST_BIN(senderPipeline), "appsink-video");
    appsinkAudio = gst_bin_get_by_name(GST_BIN(senderPipeline), "appsink-audio");

    if (!(appsinkVideo != NULL || appsinkAudio != NULL)) {
        LogError(("Failed to get appsink elements"));
        goto CleanUp;
    }

    LogInfo(("Connecting GStreamer callbacks"));
    if (appsinkVideo != NULL) {
        g_signal_connect(appsinkVideo, "new-sample", G_CALLBACK(on_new_sample_video), pDemoContext);
    }
    if (appsinkAudio != NULL) {
        g_signal_connect(appsinkAudio, "new-sample", G_CALLBACK(on_new_sample_audio), pDemoContext);
    }

    LogInfo(("Setting pipeline to PLAYING state"));
    gst_element_set_state(senderPipeline, GST_STATE_PLAYING);

    bus = gst_element_get_bus(senderPipeline);
    while (keepRunning) {
        msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
                                       GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
        if (msg != NULL) {
            switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ERROR: {
                    GError *err;
                    gchar *debug;
                    gst_message_parse_error(msg, &err, &debug);
                    LogError(("GStreamer error: %s", err->message));
                    g_error_free(err);
                    g_free(debug);
                    keepRunning = 0;
                    break;
                }
                case GST_MESSAGE_EOS:
                    LogInfo(("End of stream"));
                    keepRunning = 0;
                    break;
                default:
                    break;
            }
            gst_message_unref(msg);
        }

        // Check if we have active peer connections
        uint32_t activeConnections = 0;
        for (int i = 0; i < AWS_MAX_VIEWER_NUM; i++) {
            if (pDemoContext->peerConnectionSessions[i].peerConnectionSession.state ==
                PEER_CONNECTION_SESSION_STATE_CONNECTION_READY) {
                activeConnections++;
            }
        }

        if (activeConnections == 0) {
            // Optional: add some delay when no connections to avoid busy waiting
            usleep(100000);  // 100ms
        }
    }

CleanUp:
    if (senderPipeline != NULL) {
        gst_element_set_state(senderPipeline, GST_STATE_NULL);
        gst_object_unref(senderPipeline);
        senderPipeline = NULL;
    }

    if (appsinkAudio != NULL) {
        gst_object_unref(appsinkAudio);
    }

    if (appsinkVideo != NULL) {
        gst_object_unref(appsinkVideo);
    }

    return (void*)(intptr_t)retStatus;
}

// GStreamer initialization function
static int32_t initializeGStreamer(DemoContext_t* pDemoContext)
{
    int32_t ret = 0;

    // Initialize GStreamer
    gst_init(NULL, NULL);
    LogInfo(("GStreamer initialized"));

    // Initialize app media source with our media hook
    ret = AppMediaSource_Init(&pDemoContext->appMediaSourcesContext,
                            OnMediaSinkHook,
                            pDemoContext);
    if (ret != 0) {
        LogError(("Failed to initialize media source"));
        return ret;
    }

    // Create thread for GStreamer pipeline
    pthread_t gstThread;
    if (pthread_create(&gstThread, NULL, sendGstreamerAudioVideo, pDemoContext) != 0) {
        LogError(("Failed to create GStreamer thread"));
        return -1;
    }
    pthread_detach(gstThread);

    return ret;
}

static int OnSignalingMessageReceived(SignalingMessage_t* pSignalingMessage,
    void* pUserData)
{
LogDebug(("Received Message from websocket server!"));
LogDebug(("Message Type: %x", pSignalingMessage->messageType));
LogDebug(("Sender ID: %.*s", (int)pSignalingMessage->remoteClientIdLength,
 pSignalingMessage->pRemoteClientId));
LogDebug(("Message: %.*s", (int)pSignalingMessage->messageLength,
pSignalingMessage->pMessage));

switch (pSignalingMessage->messageType) {
case SIGNALING_TYPE_MESSAGE_SDP_OFFER:
Metric_StartEvent(METRIC_EVENT_SENDING_FIRST_FRAME);
HandleSdpOffer(&demoContext, pSignalingMessage);
break;

case SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE:
HandleRemoteCandidate(&demoContext, pSignalingMessage);
break;

case SIGNALING_TYPE_MESSAGE_RECONNECT_ICE_SERVER:
HandleIceServerReconnect(&demoContext, pSignalingMessage);
break;

case SIGNALING_TYPE_MESSAGE_SDP_ANSWER:
case SIGNALING_TYPE_MESSAGE_STATUS_RESPONSE:
default:
break;
}

return 0;
}

static void HandleSdpOffer(DemoContext_t* pDemoContext,
const SignalingMessage_t* pSignalingMessage)
{

SignalingControllerResult_t signalingControllerReturn;
const char* pSdpOfferMessage = NULL;
size_t sdpOfferMessageLength = 0;
PeerConnectionResult_t peerConnectionResult;
PeerConnectionBufferSessionDescription_t bufferSessionDescription;
size_t formalSdpMessageLength = 0;
size_t sdpAnswerMessageLength = 0;
DemoPeerConnectionSession_t* pPcSession = NULL;
SignalingMessage_t signalingMessageSdpAnswer;

if ((pDemoContext == NULL) || (pSignalingMessage == NULL)) {
LogError(("Invalid input"));
return;
}

// Extract SDP offer content
signalingControllerReturn = SignalingController_ExtractSdpOfferFromSignalingMessage(
pSignalingMessage->pMessage,
pSignalingMessage->messageLength,
&pSdpOfferMessage,
&sdpOfferMessageLength);

if (signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK) {
LogError(("Failed to extract SDP offer"));
return;
}

// Convert newlines to formal SDP format
formalSdpMessageLength = PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH;
signalingControllerReturn = SignalingController_DeserializeSdpContentNewline(
pSdpOfferMessage,
sdpOfferMessageLength,
pDemoContext->sdpBuffer,
&formalSdpMessageLength);

if (signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK) {
LogError(("Failed to deserialize SDP offer"));
return;
}

// Get or create peer connection session
pPcSession = GetCreatePeerConnectionSession(
pDemoContext,
pSignalingMessage->pRemoteClientId,
pSignalingMessage->remoteClientIdLength,
1);

if (pPcSession == NULL) {
LogError(("Failed to create peer connection session"));
return;
}

// Set remote description (SDP offer)
bufferSessionDescription.pSdpBuffer = pDemoContext->sdpBuffer;
bufferSessionDescription.sdpBufferLength = formalSdpMessageLength;
bufferSessionDescription.type = SDP_CONTROLLER_MESSAGE_TYPE_OFFER;

peerConnectionResult = PeerConnection_SetRemoteDescription(
&pPcSession->peerConnectionSession,
&bufferSessionDescription);

if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogError(("Failed to set remote description"));
return;
}

// Create SDP answer
pDemoContext->sdpConstructedBufferLength = PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH;
peerConnectionResult = PeerConnection_CreateAnswer(
&pPcSession->peerConnectionSession,
&bufferSessionDescription,
pDemoContext->sdpConstructedBuffer,
&pDemoContext->sdpConstructedBufferLength);

if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogError(("Failed to create answer"));
return;
}

// Serialize SDP answer
sdpAnswerMessageLength = PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH;
signalingControllerReturn = SignalingController_SerializeSdpContentNewline(
pDemoContext->sdpConstructedBuffer,
pDemoContext->sdpConstructedBufferLength,
pDemoContext->sdpBuffer,
&sdpAnswerMessageLength);

if (signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK) {
LogError(("Failed to serialize SDP answer"));
return;
}

// Send SDP answer
signalingMessageSdpAnswer = (SignalingMessage_t){
.messageType = SIGNALING_TYPE_MESSAGE_SDP_ANSWER,
.pMessage = pDemoContext->sdpBuffer,
.messageLength = sdpAnswerMessageLength,
.pRemoteClientId = pSignalingMessage->pRemoteClientId,
.remoteClientIdLength = pSignalingMessage->remoteClientIdLength
};

signalingControllerReturn = SignalingController_SendMessage(
&pDemoContext->signalingControllerContext,
&signalingMessageSdpAnswer);

if (signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK) {
LogError(("Failed to send SDP answer"));
}
}

static void HandleRemoteCandidate(DemoContext_t* pDemoContext,
const SignalingMessage_t* pSignalingMessage)
{
DemoPeerConnectionSession_t* pPcSession = GetCreatePeerConnectionSession(
pDemoContext,
pSignalingMessage->pRemoteClientId,
pSignalingMessage->remoteClientIdLength,
0);

if (pPcSession == NULL) {
LogError(("No session found for ICE candidate"));
return;
}

PeerConnectionResult_t result = PeerConnection_AddRemoteCandidate(
&pPcSession->peerConnectionSession,
pSignalingMessage->pMessage,
pSignalingMessage->messageLength);

if (result != PEER_CONNECTION_RESULT_OK) {
LogError(("Failed to add remote ICE candidate"));
}
}

static void HandleLocalCandidateReady(void* pCustomContext,
    PeerConnectionIceLocalCandidate_t* pIceLocalCandidate)
{
DemoPeerConnectionSession_t* pPcSession = (DemoPeerConnectionSession_t*)pCustomContext;
char candidateJson[DEMO_ICE_CANDIDATE_JSON_MAX_LENGTH];
char candidateString[DEMO_ICE_CANDIDATE_JSON_MAX_LENGTH];
int written;

if (!pPcSession || !pIceLocalCandidate) {
LogError(("Invalid local candidate parameters"));
return;
}

// Format ICE candidate
if (pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.family == STUN_ADDRESS_IPv4) {
written = snprintf(candidateString, sizeof(candidateString),
DEMO_ICE_CANDIDATE_JSON_IPV4_TEMPLATE,
pIceLocalCandidate->localCandidateIndex,
pIceLocalCandidate->pLocalCandidate->priority,
pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[0],
pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[1],
pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[2],
pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[3],
pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.port,
GetCandidateTypeString(pIceLocalCandidate->pLocalCandidate->candidateType));
} else {
// Handle IPv6 case
written = snprintf( candidateString, DEMO_JSON_CANDIDATE_MAX_LENGTH, DEMO_ICE_CANDIDATE_JSON_IPV6_TEMPLATE,
    pIceLocalCandidate->localCandidateIndex,
    pIceLocalCandidate->pLocalCandidate->priority,
    pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[0], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[1], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[2], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[3],
    pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[4], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[5], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[6], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[7],
    pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[8], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[9], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[10], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[11],
    pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[12], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[13], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[14], pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.address[15],
    pIceLocalCandidate->pLocalCandidate->endpoint.transportAddress.port,
    GetCandidateTypeString( pIceLocalCandidate->pLocalCandidate->candidateType ) );
/* IPv6 formatting parameters */
}

if (written < 0 || written >= sizeof(candidateString)) {
LogError(("Failed to format ICE candidate"));
return;
}

// Create ICE candidate message
written = snprintf(candidateJson, sizeof(candidateJson),
DEMO_ICE_CANDIDATE_JSON_TEMPLATE,
(int)strlen(candidateString), candidateString);

if (written < 0 || written >= sizeof(candidateJson)) {
LogError(("Failed to create ICE candidate JSON"));
return;
}

// Send the candidate
SignalingMessage_t message = {
.messageType = SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE,
.pMessage = candidateJson,
.messageLength = written,
.pRemoteClientId = pPcSession->remoteClientId,
.remoteClientIdLength = pPcSession->remoteClientIdLength
};

SignalingControllerResult_t result = SignalingController_SendMessage(
&demoContext.signalingControllerContext,
&message);

if (result != SIGNALING_CONTROLLER_RESULT_OK) {
LogError(("Failed to send ICE candidate"));
}
}

static int32_t InitializePeerConnectionSession(DemoContext_t* pDemoContext,
    DemoPeerConnectionSession_t* pDemoSession)
{
PeerConnectionSessionConfiguration_t pcConfig = {0};
pcConfig.canTrickleIce = 1U;
pcConfig.natTraversalConfigBitmap = ICE_CANDIDATE_NAT_TRAVERSAL_CONFIG_ALLOW_ALL;

PeerConnectionResult_t peerConnectionResult = PeerConnection_Init(&pDemoSession->peerConnectionSession, &pcConfig);
if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogWarn(("PeerConnection_Init fail, result: %d", peerConnectionResult));
return -1;
}

#if ENABLE_TWCC_SUPPORT
peerConnectionResult = PeerConnection_SetSenderBandwidthEstimationCallback(
&pDemoSession->peerConnectionSession,
SampleSenderBandwidthEstimationHandler,
&pDemoSession->peerConnectionSession.twccMetaData);
if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogError(("Failed to set Sender Bandwidth Estimation Callback, result: %d", peerConnectionResult));
return -1;
}
#endif

return 0;
}

static int32_t StartPeerConnectionSession(DemoContext_t* pDemoContext,
DemoPeerConnectionSession_t* pDemoSession,
const char* pRemoteClientId,
size_t remoteClientIdLength)
{
if (remoteClientIdLength > REMOTE_ID_MAX_LENGTH) {
LogWarn(("Remote client ID length (%zu) is too long", remoteClientIdLength));
return -1;
}

PeerConnectionSessionConfiguration_t pcConfig = {0};
pcConfig.iceServersCount = ICE_CONTROLLER_MAX_ICE_SERVER_COUNT;
pcConfig.canTrickleIce = 1U;
pcConfig.natTraversalConfigBitmap = ICE_CANDIDATE_NAT_TRAVERSAL_CONFIG_ALLOW_ALL;

int32_t ret = GetIceServerList(pDemoContext, pcConfig.iceServers, &pcConfig.iceServersCount);
if (ret != 0) {
return ret;
}

PeerConnectionResult_t peerConnectionResult = PeerConnection_AddIceServerConfig(
&pDemoSession->peerConnectionSession, &pcConfig);
if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogWarn(("PeerConnection_AddIceServerConfig fail, result: %d", peerConnectionResult));
return -1;
}

peerConnectionResult = PeerConnection_SetOnLocalCandidateReady(
&pDemoSession->peerConnectionSession, HandleLocalCandidateReady, pDemoSession);
if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogWarn(("PeerConnection_SetOnLocalCandidateReady fail, result: %d", peerConnectionResult));
return -1;
}

// Add video and audio transceivers
Transceiver_t* pVideoTransceiver = &pDemoSession->transceivers[DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO];
Transceiver_t* pAudioTransceiver = &pDemoSession->transceivers[DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO];

ret = AppMediaSource_InitVideoTransceiver(&pDemoContext->appMediaSourcesContext, pVideoTransceiver);
if (ret != 0) {
LogError(("Failed to initialize video transceiver"));
return ret;
}

ret = AppMediaSource_InitAudioTransceiver(&pDemoContext->appMediaSourcesContext, pAudioTransceiver);
if (ret != 0) {
LogError(("Failed to initialize audio transceiver"));
return ret;
}

peerConnectionResult = PeerConnection_AddTransceiver(&pDemoSession->peerConnectionSession, pVideoTransceiver);
if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogError(("Failed to add video transceiver, result = %d", peerConnectionResult));
return -1;
}

peerConnectionResult = PeerConnection_AddTransceiver(&pDemoSession->peerConnectionSession, pAudioTransceiver);
if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogError(("Failed to add audio transceiver, result = %d", peerConnectionResult));
return -1;
}

pDemoSession->remoteClientIdLength = remoteClientIdLength;
memcpy(pDemoSession->remoteClientId, pRemoteClientId, remoteClientIdLength);

peerConnectionResult = PeerConnection_Start(&pDemoSession->peerConnectionSession);
if (peerConnectionResult != PEER_CONNECTION_RESULT_OK) {
LogError(("Failed to start peer connection, result = %d", peerConnectionResult));
return -1;
}

return 0;
}


#if ENABLE_SCTP_DATA_CHANNEL

#if ( DATACHANNEL_CUSTOM_CALLBACK_HOOK != 0 )
        static void OnDataChannelMessage( PeerConnectionDataChannel_t * pDataChannel,
                                          uint8_t isBinary,
                                          uint8_t * pMessage,
                                          uint32_t pMessageLen )
        {
            char ucSendMessage[DEFAULT_DATA_CHANNEL_ON_MESSAGE_BUFFER_SIZE];
            PeerConnectionResult_t retStatus = PEER_CONNECTION_RESULT_OK;
            if( ( pMessage == NULL ) || ( pDataChannel == NULL ) )
            {
                LogError( ( "No message or pDataChannel received in OnDataChannelMessage" ) );
                return;
            }

            if( isBinary )
            {
                LogWarn( ( "[VIEWER] [Peer: %s Channel Name: %s] >>> DataChannel Binary Message",
                           pDataChannel->pPeerConnection->combinedName,
                           pDataChannel->ucDataChannelName ) );
            }
            else {
                LogWarn( ( "[VIEWER] [Peer: %s Channel Name: %s] >>> DataChannel String Message: %.*s\n",
                           pDataChannel->pPeerConnection->combinedName,
                           pDataChannel->ucDataChannelName,
                           ( int ) pMessageLen, pMessage ) );

                sprintf( ucSendMessage, "Received %ld bytes, ECHO: %.*s", ( long int ) pMessageLen, ( int ) ( pMessageLen > ( DEFAULT_DATA_CHANNEL_ON_MESSAGE_BUFFER_SIZE - 128 ) ? ( DEFAULT_DATA_CHANNEL_ON_MESSAGE_BUFFER_SIZE - 128 ) : pMessageLen ), pMessage );
                retStatus = PeerConnectionSCTP_DataChannelSend( pDataChannel, 0U, ( uint8_t * ) ucSendMessage, strlen( ucSendMessage ) );
            }

            if( retStatus != PEER_CONNECTION_RESULT_OK )
            {
                LogWarn( ( "[KVS Master] OnDataChannelMessage(): operation returned status code: 0x%08x \n", ( unsigned int ) retStatus ) );
            }

        }

        OnDataChannelMessageReceived_t PeerConnectionSCTP_SetChannelOnMessageCallbackHook( PeerConnectionSession_t * pPeerConnectionSession,
                                                                                           uint32_t ulChannelId,
                                                                                           const uint8_t * pucName,
                                                                                           uint32_t ulNameLen )
        {
            ( void ) pPeerConnectionSession;
            ( void ) ulChannelId;
            ( void ) pucName;
            ( void ) ulNameLen;

            return OnDataChannelMessage;
        }

#endif /* (DATACHANNEL_CUSTOM_CALLBACK_HOOK != 0) */

#endif /* ENABLE_SCTP_DATA_CHANNEL */

int main()
{
int ret = 0;
SignalingControllerResult_t signalingControllerReturn;
SignalingControllerConnectInfo_t connectInfo = {0};
SSLCredentials_t sslCreds = {0};

// Initialize random seed
srand(time(NULL));

// Initialize SRTP
srtp_init();

#if ENABLE_SCTP_DATA_CHANNEL
Sctp_Init();
#endif

memset(&demoContext, 0, sizeof(DemoContext_t));

// Set up SSL credentials
sslCreds.pCaCertPath = AWS_CA_CERT_PATH;
#if defined(AWS_IOT_THING_ROLE_ALIAS)
sslCreds.pDeviceCertPath = AWS_IOT_THING_CERT_PATH;
sslCreds.pDeviceKeyPath = AWS_IOT_THING_PRIVATE_KEY_PATH;
#endif

// Set up connection info
connectInfo.awsConfig.pRegion = AWS_REGION;
connectInfo.awsConfig.regionLen = strlen(AWS_REGION);
connectInfo.awsConfig.pService = "kinesisvideo";
connectInfo.awsConfig.serviceLen = strlen("kinesisvideo");
connectInfo.channelName.pChannelName = AWS_KVS_CHANNEL_NAME;
connectInfo.channelName.channelNameLength = strlen(AWS_KVS_CHANNEL_NAME);
connectInfo.pUserAgentName = AWS_KVS_AGENT_NAME;
connectInfo.userAgentNameLength = strlen(AWS_KVS_AGENT_NAME);
connectInfo.messageReceivedCallback = OnSignalingMessageReceived;
connectInfo.pMessageReceivedCallbackData = &demoContext;

#if defined(AWS_ACCESS_KEY_ID)
connectInfo.awsCreds.pAccessKeyId = AWS_ACCESS_KEY_ID;
connectInfo.awsCreds.accessKeyIdLen = strlen(AWS_ACCESS_KEY_ID);
connectInfo.awsCreds.pSecretAccessKey = AWS_SECRET_ACCESS_KEY;
connectInfo.awsCreds.secretAccessKeyLen = strlen(AWS_SECRET_ACCESS_KEY);
#if defined(AWS_SESSION_TOKEN)
connectInfo.awsCreds.pSessionToken = AWS_SESSION_TOKEN;
connectInfo.awsCreds.sessionTokenLength = strlen(AWS_SESSION_TOKEN);
#endif
#endif

#if defined(AWS_IOT_THING_ROLE_ALIAS)
connectInfo.awsIotCreds.pIotCredentialsEndpoint = AWS_CREDENTIALS_ENDPOINT;
connectInfo.awsIotCreds.iotCredentialsEndpointLength = strlen(AWS_CREDENTIALS_ENDPOINT);
connectInfo.awsIotCreds.pThingName = AWS_IOT_THING_NAME;
connectInfo.awsIotCreds.thingNameLength = strlen(AWS_IOT_THING_NAME);
connectInfo.awsIotCreds.pRoleAlias = AWS_IOT_THING_ROLE_ALIAS;
connectInfo.awsIotCreds.roleAliasLength = strlen(AWS_IOT_THING_ROLE_ALIAS);
#endif

// Initialize signaling
signalingControllerReturn = SignalingController_Init(&demoContext.signalingControllerContext, &sslCreds);
if (signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK) {
LogError(("Failed to initialize signaling controller"));
ret = -1;
goto CleanUp;
}

// Set up signal handler
signal(SIGINT, terminateHandler);

// Initialize metrics
Metric_Init();

// Initialize GStreamer and media source
ret = initializeGStreamer(&demoContext);
if (ret != 0) {
LogError(("Failed to initialize GStreamer"));
goto CleanUp;
}

// Initialize peer connection sessions
for (int i = 0; i < AWS_MAX_VIEWER_NUM; i++) {
ret = InitializePeerConnectionSession(&demoContext, &demoContext.peerConnectionSessions[i]);
if (ret != 0) {
LogError(("Failed to initialize peer connection session %d", i));
goto CleanUp;
}
}

// Start signaling
signalingControllerReturn = SignalingController_StartListening(
&demoContext.signalingControllerContext, &connectInfo);
if (signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK) {
LogError(("Failed to start signaling"));
ret = -1;
goto CleanUp;
}

// Main loop
while (keepRunning) {
sleep(1);
}

CleanUp:
// Clean up GStreamer
if (senderPipeline != NULL) {
gst_element_set_state(senderPipeline, GST_STATE_NULL);
gst_object_unref(senderPipeline);
}
gst_deinit();

LogInfo(("Cleanup completed"));

return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
