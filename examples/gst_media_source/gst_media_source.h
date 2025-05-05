#ifndef GST_MEDIA_SOURCE_H
#define GST_MEDIA_SOURCE_H

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
} WebrtcFrame_t;

// Frame flags
#define FRAME_FLAG_NONE 0
#define FRAME_FLAG_KEY_FRAME 1

typedef struct GstMediaSourcesContext GstMediaSourcesContext_t;
typedef int32_t (* GstMediaSourceOnMediaSinkHook)( void * pCustom,
                                                   WebrtcFrame_t * pFrame );

typedef struct GstMediaSourceContext
{
    uint32_t numReadyPeer;
    TransceiverTrackKind_t trackKind;
    GstMediaSourcesContext_t * pSourcesContext;

    /* GStreamer pipeline elements */
    GstElement * pipeline;
    GstElement * appsink;
    GstElement * encoder;
    GMainLoop * main_loop;
} GstMediaSourceContext_t;

typedef struct GstMediaSourcesContext {
    GstMediaSourceContext_t videoContext;
    GstMediaSourceContext_t audioContext;
    pthread_mutex_t mediaMutex;
    GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc;
    void * pOnMediaSinkHookCustom;
} GstMediaSourcesContext_t;

/**
 * @brief Initialize media source context
 */
int32_t GstMediaSource_Init( GstMediaSourcesContext_t * pCtx,
                             GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                             void * pOnMediaSinkHookCustom );

/**
 * @brief Initialize video transceiver
 */
int32_t GstMediaSource_InitVideoTransceiver( GstMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pVideoTranceiver );

/**
 * @brief Initialize audio transceiver
 */
int32_t GstMediaSource_InitAudioTransceiver( GstMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pAudioTranceiver );

#endif /* GST_MEDIA_SOURCE_H */
