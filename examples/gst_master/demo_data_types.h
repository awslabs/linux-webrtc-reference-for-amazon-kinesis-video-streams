#ifndef DEMO_DATA_TYPES_H
#define DEMO_DATA_TYPES_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include "sdp_controller.h"
#include "signaling_controller.h"
#include "peer_connection.h"
#include "gst_media_source.h"

#define DEMO_SDP_BUFFER_MAX_LENGTH ( 10000 )
#define DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO ( 0 )
#define DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO ( 1 )
#define REMOTE_ID_MAX_LENGTH    ( 256 )

typedef struct DemoPeerConnectionSession
{
    /* The remote client ID, representing the remote peer, from signaling message. */
    char remoteClientId[ REMOTE_ID_MAX_LENGTH ];
    size_t remoteClientIdLength;

    /* Configuration. */
    uint8_t canTrickleIce;

    /* Peer connection session. */
    PeerConnectionSession_t peerConnectionSession;
    Transceiver_t transceivers[ PEER_CONNECTION_TRANSCEIVER_MAX_COUNT ];
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
    GstMediaSourcesContext_t mediaSourceContext;

} DemoContext_t;

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* DEMO_DATA_TYPES_H */
