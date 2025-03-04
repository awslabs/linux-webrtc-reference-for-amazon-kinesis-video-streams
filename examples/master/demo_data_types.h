#ifndef DEMO_DATA_TYPES_H
#define DEMO_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "sdp_controller.h"
#include "signaling_controller.h"
#include "peer_connection.h"
#include "app_media_source.h"

#define DEMO_SDP_BUFFER_MAX_LENGTH ( 10000 )
#define REMOTE_ID_MAX_LENGTH    ( 256 )
typedef struct DemoPeerConnectionSession
{
    /* The remote client ID, representing the remote peer, from signaling message. */
    char remoteClientId[ REMOTE_ID_MAX_LENGTH ];
    size_t remoteClientIdLength;

    /* Configuration. */
    uint8_t isInUse;
    uint8_t canTrickleIce;

    /* Peer connection session. */
    PeerConnectionSession_t peerConnectionSession;
} DemoPeerConnectionSession_t;

typedef struct DemoContext
{
    /* Signaling controller. */
    SignalingControllerContext_t signalingControllerContext;

    /* SDP buffers. */
    char sdpConstructedBuffer[ PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH ];
    size_t sdpConstructedBufferLength;

    char sdpBuffer[ PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH ];

    /* Peer Connection. */
    DemoPeerConnectionSession_t peerConnectionSessions[ AWS_MAX_VIEWER_NUM ];
    AppMediaSourcesContext_t appMediaSourcesContext;
} DemoContext_t;

#ifdef __cplusplus
}
#endif

#endif /* DEMO_DATA_TYPES_H */