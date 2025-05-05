#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdio.h>
#include "sdp_controller.h"
#include "signaling_controller.h"
#include "peer_connection.h"

#define DEMO_SDP_BUFFER_MAX_LENGTH ( 10000 )
#define DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO ( 0 )
#define DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO ( 1 )
#define REMOTE_ID_MAX_LENGTH    ( 256 )

struct AppMediaSourcesContext;
typedef struct AppMediaSourcesContext AppMediaSourcesContext_t;

typedef int32_t ( * InitTransceiverFunc_t )( void * pCtx, TransceiverTrackKind_t trackKind, Transceiver_t * pTranceiver );

typedef struct AppSession
{
    /* The remote client ID, representing the remote peer, from signaling message. */
    char remoteClientId[ REMOTE_ID_MAX_LENGTH ];
    size_t remoteClientIdLength;

    /* Configuration. */
    uint8_t canTrickleIce;

    /* Peer connection session. */
    PeerConnectionSession_t peerConnectionSession;
    Transceiver_t transceivers[ PEER_CONNECTION_TRANSCEIVER_MAX_COUNT ];

    /* Initialized signaling controller. */
    SignalingControllerContext_t * pSignalingControllerContext;
} AppSession_t;

typedef struct AppContext
{
    /* Signaling controller. */
    SignalingControllerContext_t signalingControllerContext;

    /* SDP buffers. */
    char sdpConstructedBuffer[ PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH ];
    size_t sdpConstructedBufferLength;

    char sdpBuffer[ PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH ];

    /* Peer Connection. */
    AppSession_t appSessions[ AWS_MAX_VIEWER_NUM ];

    /* Media context. */
    InitTransceiverFunc_t initTransceiverFunc;
    AppMediaSourcesContext_t * pAppMediaSourcesContext;
} AppContext_t;

int AppCommon_Init( AppContext_t * pAppContext, InitTransceiverFunc_t initTransceiverFunc, void * pMediaContext );
int AppCommon_Start( AppContext_t * pAppContext );

#endif /* APP_COMMON_H */
