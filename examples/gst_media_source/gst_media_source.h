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

#define DEFAULT_VIDEO_WIDTH 1280
#define DEFAULT_VIDEO_HEIGHT 720
#define DEFAULT_VIDEO_FRAMERATE 25
#define DEFAULT_AUDIO_SAMPLE_RATE 48000
#define DEFAULT_AUDIO_CHANNELS 2
#define DEFAULT_VIDEO_BITRATE 512
#define DEFAULT_AUDIO_BITRATE 64

// Frame flags
#define FRAME_FLAG_NONE 0
#define FRAME_FLAG_KEY_FRAME 1

typedef struct GstMediaSourcesContext GstMediaSourcesContext_t;
typedef int32_t (* GstMediaSourceOnMediaSinkHook)( void * pCustom,
                                                  webrtc_frame_t * pFrame );

typedef struct GstMediaSourceContext
{
    MessageQueueHandler_t dataQueue;
    uint8_t numReadyPeer;
    TransceiverTrackKind_t trackKind;
    GstMediaSourcesContext_t * pSourcesContext;

    /* GStreamer pipeline elements */
    GstElement *pipeline;
    GstElement *appsink;
    GstElement *encoder;    // Reference to encoder for bitrate control
    GstSample *sample;

    /* GStreamer pipeline configuration */
    gchar *pipeline_description;
    guint width;           // Video width (for video only)
    guint height;          // Video height (for video only)
    guint framerate;       // Frames per second (for video only)
    guint sample_rate;     // Audio sample rate (for audio only)
    guint channels;        // Number of audio channels (for audio only)
    guint bitrate;         // Encoding bitrate
    gboolean is_running;   // Pipeline state flag
} GstMediaSourceContext_t;

typedef struct GstMediaSourcesContext
{
    pthread_mutex_t mediaMutex;
    GstMediaSourceContext_t videoContext;
    GstMediaSourceContext_t audioContext;
    GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc;
    void * pOnMediaSinkHookCustom;
    gboolean enableVideo;
    gboolean enableAudio;
} GstMediaSourcesContext_t;

/**
 * @brief Initialize media source context
 */
int32_t GstMediaSource_Init(GstMediaSourcesContext_t *pCtx,
                           GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                           void *pOnMediaSinkHookCustom,
                           gboolean enableVideo,
                           gboolean enableAudio);

/**
 * @brief Initialize video pipeline
 */
int32_t GstMediaSource_InitVideoGstreamer(GstMediaSourceContext_t *pCtx,
                                         guint width,
                                         guint height,
                                         guint framerate,
                                         guint bitrate,
                                         const gchar *pipeline_desc);

/**
 * @brief Initialize audio pipeline
 */
int32_t GstMediaSource_InitAudioGstreamer(GstMediaSourceContext_t *pCtx,
                                         guint sample_rate,
                                         guint channels,
                                         guint bitrate,
                                         const gchar *pipeline_desc);

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
 * @brief Set video encoder bitrate
 */
int32_t GstMediaSource_SetVideoBitrate(GstMediaSourceContext_t *pCtx, guint bitrate);

/**
 * @brief Set audio encoder bitrate
 */
int32_t GstMediaSource_SetAudioBitrate(GstMediaSourceContext_t *pCtx, guint bitrate);

/**
 * @brief Cleanup GStreamer resources
 */
void GstMediaSource_Cleanup(GstMediaSourceContext_t *pCtx);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* GST_MEDIA_SOURCE_H */
