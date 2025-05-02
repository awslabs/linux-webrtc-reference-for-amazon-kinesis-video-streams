#ifndef GST_MEDIA_SOURCE_H
#define GST_MEDIA_SOURCE_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "message_queue.h"
#include "peer_connection.h"

typedef struct {
    uint8_t * pData;
    uint32_t size;
    uint64_t timestampUs;
    TransceiverTrackKind_t trackKind;
    uint8_t flags;
    uint8_t freeData;  /* indicate user need to free pData after using it */
} webrtc_frame_t;

// Frame flags
#define FRAME_FLAG_NONE 0
#define FRAME_FLAG_KEY_FRAME 1

typedef struct GstMediaSourcesContext GstMediaSourcesContext_t;
typedef int32_t (* GstMediaSourceOnMediaSinkHook)( void * pCustom,
                                                  webrtc_frame_t * pFrame );

typedef struct GstMediaSourceContext
{
    uint32_t numReadyPeer;
    TransceiverTrackKind_t trackKind;
    GstMediaSourcesContext_t * pSourcesContext;

    /* GStreamer pipeline elements */
    GstElement *pipeline;
    GstElement *appsink;
    GstElement *encoder;    // Reference to encoder for bitrate control

    gboolean is_running;   // Pipeline state flag

    GMainLoop *main_loop;
} GstMediaSourceContext_t;

struct GstMediaSourcesContext {
    GstMediaSourceContext_t videoContext;
    GstMediaSourceContext_t audioContext;
    pthread_mutex_t mediaMutex;
    GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc;
    void* pOnMediaSinkHookCustom;
};

/**
 * @brief Initialize media source context
 */
int32_t GstMediaSource_Init(GstMediaSourcesContext_t *pCtx,
                           GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                           void *pOnMediaSinkHookCustom);

/**
 * @brief Initialize GStreamer pipeline
 */
int32_t GstMediaSource_InitPipeline(GstMediaSourcesContext_t* pCtx);

/**
 * @brief Initialize video transceiver
 */
int32_t GstMediaSource_InitVideoTransceiver(GstMediaSourcesContext_t *pCtx,
                                           Transceiver_t *pVideoTranceiver);

/**
 * @brief Initialize audio transceiver
 */
int32_t GstMediaSource_InitAudioTransceiver(GstMediaSourcesContext_t *pCtx,
                                           Transceiver_t *pAudioTranceiver);

/**
 * @brief Cleanup GStreamer resources
 */
void GstMediaSource_Cleanup(GstMediaSourcesContext_t* pCtx);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* GST_MEDIA_SOURCE_H */
