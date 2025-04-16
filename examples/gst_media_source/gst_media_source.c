#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "gst_media_source.h"
#include "logging.h"

#define DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND (3)
#define DEFAULT_TRANSCEIVER_VIDEO_BIT_RATE (4 * 1024 * 1024)
#define DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE (1000 * 1024)
#define SAMPLE_AUDIO_FRAME_DURATION_IN_US (20 * 1000)
#define SAMPLE_VIDEO_FRAME_DURATION_IN_US ((1000 * 1000) / 25) // Assuming 25 FPS


static void* VideoTx_Task(void* pParameter)
{
    GstMediaSourceContext_t* pVideoContext = (GstMediaSourceContext_t*)pParameter;
    webrtc_frame_t frame;
    GstBuffer* buffer;
    GstMapInfo map;
    GstSample* sample;
    GstSegment* segment;
    uint8_t isDroppable;

    if (pVideoContext == NULL)
    {
        LogError(("Invalid input, pVideoContext: %p", pVideoContext));
        return NULL;
    }

    frame.timestampUs = 0;
    frame.freeData = 0;

    // Start the pipeline
    gst_element_set_state(pVideoContext->pipeline, GST_STATE_PLAYING);
    pVideoContext->is_running = TRUE;

    while (pVideoContext->is_running)
    {
        if (pVideoContext->numReadyPeer != 0)
        {
            sample = gst_app_sink_pull_sample(GST_APP_SINK(pVideoContext->appsink));

            if (sample)
            {
                buffer = gst_sample_get_buffer(sample);

                // Check for droppable frames
                isDroppable = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED) ||
                            GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DECODE_ONLY) ||
                            (GST_BUFFER_FLAGS(buffer) == GST_BUFFER_FLAG_DISCONT) ||
                            !GST_BUFFER_PTS_IS_VALID(buffer);

                if (!isDroppable)
                {
                    gst_buffer_map(buffer, &map, GST_MAP_READ);

                    // Set key frame flag
                    frame.flags = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT) ?
                                FRAME_FLAG_NONE : FRAME_FLAG_KEY_FRAME;

                    // Get proper timestamp
                    segment = gst_sample_get_segment(sample);
                    frame.timestampUs = gst_segment_to_running_time(segment,
                                                              GST_FORMAT_TIME,
                                                              buffer->pts) / 1000; // Convert ns to us

                    frame.pData = map.data;
                    frame.size = map.size;
                    frame.trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;

                    if (pVideoContext->pSourcesContext->onMediaSinkHookFunc)
                    {
                        (void)pVideoContext->pSourcesContext->onMediaSinkHookFunc(
                            pVideoContext->pSourcesContext->pOnMediaSinkHookCustom, &frame);
                    }

                    gst_buffer_unmap(buffer, &map);
                }
                gst_sample_unref(sample);
            }
        }
        usleep(SAMPLE_VIDEO_FRAME_DURATION_IN_US);
    }

    return NULL;
}

static void* AudioTx_Task(void* pParameter)
{
    GstMediaSourceContext_t* pAudioContext = (GstMediaSourceContext_t*)pParameter;
    webrtc_frame_t frame;
    GstBuffer* buffer;
    GstMapInfo map;
    GstSample* sample;
    GstSegment* segment;
    uint8_t isDroppable;

    if (pAudioContext == NULL)
    {
        LogError(("Invalid input, pAudioContext: %p", pAudioContext));
        return NULL;
    }

    frame.timestampUs = 0;
    frame.freeData = 0;

    // Start the pipeline
    gst_element_set_state(pAudioContext->pipeline, GST_STATE_PLAYING);
    pAudioContext->is_running = TRUE;

    while (pAudioContext->is_running)
    {
        if (pAudioContext->numReadyPeer != 0)
        {
            sample = gst_app_sink_pull_sample(GST_APP_SINK(pAudioContext->appsink));

            if (sample)
            {
                buffer = gst_sample_get_buffer(sample);

                // Check for droppable frames
                isDroppable = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED) ||
                            GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DECODE_ONLY) ||
                            !GST_BUFFER_PTS_IS_VALID(buffer);

                if (!isDroppable)
                {
                    gst_buffer_map(buffer, &map, GST_MAP_READ);

                    // Get proper timestamp
                    segment = gst_sample_get_segment(sample);
                    frame.timestampUs = gst_segment_to_running_time(segment,
                                                              GST_FORMAT_TIME,
                                                              buffer->pts) / 1000; // Convert ns to us

                    frame.pData = map.data;
                    frame.size = map.size;
                    frame.flags = FRAME_FLAG_NONE;
                    frame.trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;

                    if (pAudioContext->pSourcesContext->onMediaSinkHookFunc)
                    {
                        (void)pAudioContext->pSourcesContext->onMediaSinkHookFunc(
                            pAudioContext->pSourcesContext->pOnMediaSinkHookCustom, &frame);
                    }

                    gst_buffer_unmap(buffer, &map);
                }
                gst_sample_unref(sample);
            }
        }
        usleep(SAMPLE_AUDIO_FRAME_DURATION_IN_US);
    }

    return NULL;
}

int32_t GstMediaSource_InitVideoGstreamer(GstMediaSourceContext_t* pCtx,
                                       guint width,
                                       guint height,
                                       guint framerate,
                                       guint bitrate,
                                       const gchar* pipeline_desc)
{
    if (!pCtx) return -1;

    pCtx->width = width;
    pCtx->height = height;
    pCtx->framerate = framerate;
    pCtx->bitrate = bitrate;

    if (pipeline_desc) {
        pCtx->pipeline_description = g_strdup(pipeline_desc);
    } else {
        pCtx->pipeline_description = g_strdup_printf(
            "autovideosrc ! "
            "queue ! videoconvert ! video/x-raw,width=%d,height=%d,framerate=%d/1 ! "
            "x264enc name=videoEncoder bframes=0 speed-preset=veryfast bitrate=%d "
            "byte-stream=TRUE tune=zerolatency ! "
            "video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline ! "
            "appsink sync=TRUE emit-signals=TRUE name=vsink",
            width, height, framerate, bitrate);
    }

    GError* error = NULL;
    pCtx->pipeline = gst_parse_launch(pCtx->pipeline_description, &error);
    if (!pCtx->pipeline) {
        LogError(("Failed to create pipeline: %s", error->message));
        g_error_free(error);
        return -1;
    }

    pCtx->appsink = gst_bin_get_by_name(GST_BIN(pCtx->pipeline), "vsink");
    if (!pCtx->appsink) {
        LogError(("Failed to get appsink element"));
        return -1;
    }

    pCtx->encoder = gst_bin_get_by_name(GST_BIN(pCtx->pipeline), "videoEncoder");
    if (!pCtx->encoder) {
        LogError(("Failed to get encoder element"));
        return -1;
    }

    g_object_set(G_OBJECT(pCtx->appsink), "emit-signals", TRUE, NULL);

    return 0;
}

int32_t GstMediaSource_InitAudioGstreamer(GstMediaSourceContext_t* pCtx,
                                       guint sample_rate,
                                       guint channels,
                                       guint bitrate,
                                       const gchar* pipeline_desc)
{
    if (!pCtx) return -1;

    pCtx->sample_rate = sample_rate;
    pCtx->channels = channels;
    pCtx->bitrate = bitrate;

    if (pipeline_desc) {
        pCtx->pipeline_description = g_strdup(pipeline_desc);
    } else {
        pCtx->pipeline_description = g_strdup_printf(
            "autoaudiosrc ! "
            "queue leaky=2 max-size-buffers=400 ! audioconvert ! audioresample ! "
            "opusenc name=audioEncoder bitrate=%d ! audio/x-opus,rate=%d,channels=%d ! "
            "appsink sync=TRUE emit-signals=TRUE name=asink",
            bitrate * 1000, sample_rate, channels); // opus expects bitrate in bits/sec
    }

    GError* error = NULL;
    pCtx->pipeline = gst_parse_launch(pCtx->pipeline_description, &error);
    if (!pCtx->pipeline) {
        LogError(("Failed to create pipeline: %s", error->message));
        g_error_free(error);
        return -1;
    }

    pCtx->appsink = gst_bin_get_by_name(GST_BIN(pCtx->pipeline), "asink");
    if (!pCtx->appsink) {
        LogError(("Failed to get appsink element"));
        return -1;
    }

    pCtx->encoder = gst_bin_get_by_name(GST_BIN(pCtx->pipeline), "audioEncoder");
    if (!pCtx->encoder) {
        LogError(("Failed to get encoder element"));
        return -1;
    }

    g_object_set(G_OBJECT(pCtx->appsink), "emit-signals", TRUE, NULL);

    return 0;
}

int32_t GstMediaSource_Init(GstMediaSourcesContext_t* pCtx,
                         GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                         void* pOnMediaSinkHookCustom,
                         gboolean enableVideo,
                         gboolean enableAudio)
{
    int32_t ret = 0;
    pthread_t tid;

    if (pCtx == NULL)
    {
        LogError(("Invalid input, pCtx: %p", pCtx));
        return -1;
    }

    // Initialize GStreamer
    gst_init(NULL, NULL);

    memset(pCtx, 0, sizeof(GstMediaSourcesContext_t));

    if (pthread_mutex_init(&(pCtx->mediaMutex), NULL) != 0)
    {
        LogError(("Failed to create mutex for media source"));
        return -1;
    }

    pCtx->enableVideo = enableVideo;
    pCtx->enableAudio = enableAudio;
    pCtx->onMediaSinkHookFunc = onMediaSinkHookFunc;
    pCtx->pOnMediaSinkHookCustom = pOnMediaSinkHookCustom;

    if (enableVideo)
    {
        pCtx->videoContext.pSourcesContext = pCtx;
        if (pthread_create(&tid, NULL, VideoTx_Task, &pCtx->videoContext) != 0)
        {
            LogError(("Failed to create video task"));
            ret = -1;
        }
    }

    if (ret == 0 && enableAudio)
    {
        pCtx->audioContext.pSourcesContext = pCtx;
        if (pthread_create(&tid, NULL, AudioTx_Task, &pCtx->audioContext) != 0)
        {
            LogError(("Failed to create audio task"));
            ret = -1;
        }
    }

    return ret;
}

int32_t GstMediaSource_SetVideoBitrate(GstMediaSourceContext_t* pCtx, guint bitrate)
{
    if (!pCtx || !pCtx->encoder) return -1;

    g_object_set(G_OBJECT(pCtx->encoder), "bitrate", bitrate, NULL);
    pCtx->bitrate = bitrate;

    return 0;
}

int32_t GstMediaSource_SetAudioBitrate(GstMediaSourceContext_t* pCtx, guint bitrate)
{
    if (!pCtx || !pCtx->encoder) return -1;

    g_object_set(G_OBJECT(pCtx->encoder), "bitrate", bitrate * 1000, NULL); // opus expects bits/sec
    pCtx->bitrate = bitrate;

    return 0;
}

int32_t GstMediaSource_InitVideoTransceiver(GstMediaSourcesContext_t* pCtx,
                                        Transceiver_t* pVideoTransceiver)
{
    if (!pCtx || !pVideoTransceiver) return -1;

    memset(pVideoTransceiver, 0, sizeof(Transceiver_t));
    pVideoTransceiver->trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
    pVideoTransceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDRECV;
    TRANSCEIVER_ENABLE_CODEC(pVideoTransceiver->codecBitMap,
                            TRANSCEIVER_RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_BIT);
    pVideoTransceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND;
    pVideoTransceiver->rollingbufferBitRate = DEFAULT_TRANSCEIVER_VIDEO_BIT_RATE;

    return 0;
}

int32_t GstMediaSource_InitAudioTransceiver(GstMediaSourcesContext_t* pCtx,
                                        Transceiver_t* pAudioTransceiver)
{
    if (!pCtx || !pAudioTransceiver) return -1;

    memset(pAudioTransceiver, 0, sizeof(Transceiver_t));
    pAudioTransceiver->trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
    pAudioTransceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDRECV;
    TRANSCEIVER_ENABLE_CODEC(pAudioTransceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_OPUS_BIT);
    pAudioTransceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND;
    pAudioTransceiver->rollingbufferBitRate = DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE;

    return 0;
}

void GstMediaSource_Cleanup(GstMediaSourceContext_t* pCtx)
{
    if (!pCtx) return;

    pCtx->is_running = FALSE;

    if (pCtx->pipeline)
    {
        gst_element_set_state(pCtx->pipeline, GST_STATE_NULL);
        gst_object_unref(pCtx->pipeline);
    }

    if (pCtx->encoder)
    {
        gst_object_unref(pCtx->encoder);
    }

    if (pCtx->appsink)
    {
        gst_object_unref(pCtx->appsink);
    }

    if (pCtx->pipeline_description)
    {
        g_free(pCtx->pipeline_description);
    }
}
