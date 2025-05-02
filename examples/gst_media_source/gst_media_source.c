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

#define DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND ( 3 )
#define DEFAULT_TRANSCEIVER_VIDEO_BIT_RATE ( 4 * 1024 * 1024 )
#define DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE ( 1000 * 1024 )
#define SAMPLE_AUDIO_FRAME_DURATION_IN_US ( 20 * 1000 )
#define SAMPLE_VIDEO_FRAME_DURATION_IN_US ( ( 1000 * 1000 ) / 25 ) // 25 FPS

#define DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID "myKvsVideoStream"
#define DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID "myVideoTrack"
#define DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID "myAudioTrack"

static gboolean bus_callback( GstBus * bus,
                              GstMessage * msg,
                              gpointer data )
{
    GstMediaSourceContext_t * ctx = ( GstMediaSourceContext_t * )data;

    switch( GST_MESSAGE_TYPE( msg ) ) {
        case GST_MESSAGE_ERROR: {
            GError * err;
            gchar * debug;
            gst_message_parse_error( msg,
                                     &err,
                                     &debug );
            LogError( ( "GStreamer error: %s", err->message ) );
            LogError( ( "Debug info: %s", debug ) );
            g_error_free( err );
            g_free( debug );
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError * err;
            gchar * debug;
            gst_message_parse_warning( msg,
                                       &err,
                                       &debug );
            LogWarn( ( "GStreamer warning: %s", err->message ) );
            LogWarn( ( "Debug info: %s", debug ) );
            g_error_free( err );
            g_free( debug );
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            if( GST_MESSAGE_SRC( msg ) == GST_OBJECT( ctx->pipeline ) )
            {
                GstState old_state, new_state, pending;
                gst_message_parse_state_changed( msg,
                                                 &old_state,
                                                 &new_state,
                                                 &pending );
                LogInfo( ( "Pipeline state changed from %s to %s",
                           gst_element_state_get_name( old_state ),
                           gst_element_state_get_name( new_state ) ) );
            }
            break;
        }
        default:
            break;
    }
    return TRUE;
}

static gboolean ensure_pipeline_playing( GstMediaSourceContext_t * pCtx )
{
    GstStateChangeReturn ret;
    GstState state;
    GstState pending;

    if( !pCtx || !pCtx->pipeline )
        return FALSE;

    ret = gst_element_get_state( pCtx->pipeline,
                                 &state,
                                 &pending,
                                 GST_SECOND * 5 );

    if( ret == GST_STATE_CHANGE_FAILURE )
    {
        LogError( ( "Failed to get pipeline state" ) );
        return FALSE;
    }

    if( state != GST_STATE_PLAYING )
    {
        ret = gst_element_set_state( pCtx->pipeline,
                                     GST_STATE_PLAYING );
        if( ret == GST_STATE_CHANGE_FAILURE )
        {
            LogError( ( "Failed to set pipeline to PLAYING state" ) );
            return FALSE;
        }

        ret = gst_element_get_state( pCtx->pipeline,
                                     NULL,
                                     NULL,
                                     GST_SECOND * 5 );
        if( ret == GST_STATE_CHANGE_FAILURE )
        {
            LogError( ( "Pipeline failed to reach PLAYING state" ) );
            return FALSE;
        }
    }

    return TRUE;
}

static void on_new_video_sample(GstElement *sink, gpointer user_data) {
    GstMediaSourceContext_t * pVideoContext = (GstMediaSourceContext_t *)user_data;
    webrtc_frame_t frame;
    GstBuffer *buffer;
    GstMapInfo map;
    GstSample *sample;

    #if LIBRARY_LOG_LEVEL >= LOG_DEBUG
    int frame_count = 0;
    #endif

    if (!pVideoContext || !pVideoContext->numReadyPeer) {
        return;
    }

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        return;
    }

    buffer = gst_sample_get_buffer(sample);

    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        frame.pData = map.data;
        frame.size = map.size;
        frame.flags = FRAME_FLAG_NONE;
        frame.freeData = 0;
        frame.trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;

        if (GST_BUFFER_PTS_IS_VALID(buffer)) {
            frame.timestampUs = GST_BUFFER_PTS(buffer) / 1000;
        } else {
            static uint64_t running_time = 0;
            frame.timestampUs = running_time;
            running_time += SAMPLE_VIDEO_FRAME_DURATION_IN_US;
        }

        #if DEBUG_H264_PACKETS
            debug_h264_packet(frame.pData, frame.size);
        #endif

        if (pVideoContext->pSourcesContext->onMediaSinkHookFunc) {
            LogVerbose(("Sending video frame: size=%zu, ts=%lu",
                        frame.size, frame.timestampUs));
            (void)pVideoContext->pSourcesContext->onMediaSinkHookFunc(
                pVideoContext->pSourcesContext->pOnMediaSinkHookCustom,
                &frame);
        }

        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
}

static void * VideoTx_Task(void * pParameter) {
    LogDebug(("VideoTx_Task started"));
    GstMediaSourceContext_t * pVideoContext = ( GstMediaSourceContext_t * )pParameter;

    if (!pVideoContext) {
        LogError(("Invalid audio context"));
        return NULL;
    }

    // Set up appsink properties
    g_object_set(G_OBJECT(pVideoContext->appsink),
                "emit-signals", TRUE,
                "sync", TRUE,
                NULL);

    // Connect to new-sample signal
    g_signal_connect(pVideoContext->appsink, "new-sample",
                    G_CALLBACK(on_new_video_sample), pVideoContext);

    // Create main loop
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    pVideoContext->main_loop = loop;

    // Run the main loop
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    LogDebug(("VideoTx_Task ending"));
    return NULL;
}


static void on_new_audio_sample(GstElement *sink, gpointer user_data) {
    GstMediaSourceContext_t * pAudioContext = (GstMediaSourceContext_t *)user_data;
    webrtc_frame_t frame;
    GstBuffer *buffer;
    GstMapInfo map;
    GstSample *sample;

    if (!pAudioContext || !pAudioContext->numReadyPeer) {
        return;
    }

    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        return;
    }

    buffer = gst_sample_get_buffer(sample);

    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        frame.pData = map.data;
        frame.size = map.size;
        frame.flags = FRAME_FLAG_NONE;
        frame.freeData = 0;
        frame.trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;

        if (GST_BUFFER_PTS_IS_VALID(buffer)) {
            frame.timestampUs = GST_BUFFER_PTS(buffer) / 1000;
        } else {
            static uint64_t running_time = 0;
            frame.timestampUs = running_time;
            running_time += SAMPLE_AUDIO_FRAME_DURATION_IN_US;
        }

        #if DEBUG_OPUS_PACKETS
            debug_opus_packet(frame.pData, frame.size);
        #endif

        if (pAudioContext->pSourcesContext->onMediaSinkHookFunc) {
            LogVerbose(("Sending audio frame: size=%zu, ts=%lu",
                        frame.size, frame.timestampUs));
            (void)pAudioContext->pSourcesContext->onMediaSinkHookFunc(
                pAudioContext->pSourcesContext->pOnMediaSinkHookCustom,
                &frame);
        }

        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
}

static void * AudioTx_Task(void * pParameter) {
    LogDebug(("AudioTx_Task started"));
    GstMediaSourceContext_t * pAudioContext = (GstMediaSourceContext_t *)pParameter;

    if (!pAudioContext) {
        LogError(("Invalid audio context"));
        return NULL;
    }

    // Set up appsink properties
    g_object_set(G_OBJECT(pAudioContext->appsink),
                "emit-signals", TRUE,
                "sync", TRUE,
                NULL);

    // Connect to new-sample signal
    g_signal_connect(pAudioContext->appsink, "new-sample",
                    G_CALLBACK(on_new_audio_sample), pAudioContext);

    // Create main loop
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    pAudioContext->main_loop = loop;

    // Run the main loop
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    LogDebug(("AudioTx_Task ending"));
    return NULL;
}


static int32_t HandlePcEventCallback( void * pCustomContext,
                                      TransceiverCallbackEvent_t event,
                                      TransceiverCallbackContent_t * pEventMsg )
{
    GstMediaSourceContext_t * pMediaSource = ( GstMediaSourceContext_t * )pCustomContext;
    int32_t ret = 0;

    if( !pMediaSource )
    {
        LogError( ( "Invalid media source context" ) );
        return -1;
    }

    switch( event ) {
        case TRANSCEIVER_CB_EVENT_REMOTE_PEER_READY:
            pthread_mutex_lock( &pMediaSource->pSourcesContext->mediaMutex );
            pMediaSource->numReadyPeer++;
            pthread_mutex_unlock( &pMediaSource->pSourcesContext->mediaMutex );
            LogInfo( ( "Remote peer ready for track kind %d, peers: %d",
                       pMediaSource->trackKind, pMediaSource->numReadyPeer ) );
            break;

        case TRANSCEIVER_CB_EVENT_REMOTE_PEER_CLOSED:
            pthread_mutex_lock( &pMediaSource->pSourcesContext->mediaMutex );
            if( pMediaSource->numReadyPeer > 0 )
                pMediaSource->numReadyPeer--;
            pthread_mutex_unlock( &pMediaSource->pSourcesContext->mediaMutex );
            LogInfo( ( "Remote peer closed for track kind %d, peers: %d",
                       pMediaSource->trackKind, pMediaSource->numReadyPeer ) );
            break;

        default:
            LogWarn( ( "Unknown event: %d", event ) );
            ret = -1;
            break;
    }

    return ret;
}

int32_t GstMediaSource_InitPipeline( GstMediaSourcesContext_t * pCtx )
{
    if( !pCtx )
        return -1;

    // from kvs lib
    // "autovideosrc ! queue ! videoconvert ! video/x-raw,width=1280,height=720,framerate=25/1 ! "
    // "x264enc name=sampleVideoEncoder bframes=0 speed-preset=veryfast bitrate=512 byte-stream=TRUE tune=zerolatency ! "
    // "video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline ! appsink sync=TRUE emit-signals=TRUE "
    // "name=appsink-video autoaudiosrc ! "
    // "queue leaky=2 max-size-buffers=400 ! audioconvert ! audioresample ! opusenc name=sampleAudioEncoder ! "
    // "audio/x-opus,rate=48000,channels=2 ! appsink sync=TRUE emit-signals=TRUE name=appsink-audio",

    gchar * pipeline_desc = g_strdup_printf(
        "autovideosrc ! videoconvert ! "
        "x264enc name=videoEncoder "
        "tune=zerolatency speed-preset=ultrafast "
        "key-int-max=30 bitrate=2000 bframes=0 ref=1 "
        "byte-stream=true aud=false insert-vui=true ! "
        "video/x-h264,profile=constrained-baseline,stream-format=byte-stream,alignment=au ! "
        "h264parse config-interval=1 ! "
        "queue max-size-buffers=2 ! "
        "appsink name=vsink sync=true emit-signals=true max-buffers=1 drop=true "
        "autoaudiosrc ! "
        "queue leaky=2 max-size-buffers=400 ! audioconvert ! audioresample ! opusenc name=sampleAudioEncoder ! "
        "audio/x-opus,rate=48000,channels=2 ! appsink sync=TRUE emit-signals=TRUE max-buffers=1 drop=true name=asink " );

    GError * error = NULL;
    pCtx->videoContext.pipeline = gst_parse_launch( pipeline_desc,
                                                    &error );
    g_free( pipeline_desc );

    if( !pCtx->videoContext.pipeline )
    {
        LogError( ( "Failed to create pipeline: %s", error->message ) );
        g_error_free( error );
        return -1;
    }
    // Get video sink
    pCtx->videoContext.appsink = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pipeline ),
                                                      "vsink" );
    if( !pCtx->videoContext.appsink )
    {
        LogError( ( "Failed to get video appsink" ) );
        return -1;
    }

    // Get audio sink
    pCtx->audioContext.appsink = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pipeline ),
                                                      "asink" );
    if( !pCtx->audioContext.appsink )
    {
        LogError( ( "Failed to get audio appsink" ) );
        return -1;
    }

    // Share the pipeline between video and audio contexts
    pCtx->audioContext.pipeline = pCtx->videoContext.pipeline;

    // Get encoder elements for bitrate control
    pCtx->videoContext.encoder = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pipeline ),
                                                      "videoEncoder" );
    if( !pCtx->videoContext.encoder )
    {
        LogError( ( "Failed to get video encoder element" ) );
        // Non-fatal error, continue without encoder reference
    }

    // Add bus watch
    GstBus * bus = gst_pipeline_get_bus( GST_PIPELINE( pCtx->videoContext.pipeline ) );
    gst_bus_add_watch( bus,
                       ( GstBusFunc )bus_callback,
                       pCtx );
    gst_object_unref( bus );

    return 0;
}

int32_t GstMediaSource_Init( GstMediaSourcesContext_t * pCtx,
                             GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                             void * pOnMediaSinkHookCustom )
{
    if( !pCtx )
    {
        LogError( ( "Invalid context" ) );
        return -1;
    }

    gst_init( NULL,
              NULL );
    memset( pCtx,
            0,
            sizeof( GstMediaSourcesContext_t ) );

    if( pthread_mutex_init( &pCtx->mediaMutex,
                            NULL ) != 0 )
    {
        LogError( ( "Failed to create media mutex" ) );
        return -1;
    }

    pCtx->onMediaSinkHookFunc = onMediaSinkHookFunc;
    pCtx->pOnMediaSinkHookCustom = pOnMediaSinkHookCustom;

    // Initialize contexts
    pCtx->videoContext.pSourcesContext = pCtx;
    pCtx->audioContext.pSourcesContext = pCtx;

    // Initialize single pipeline
    if( GstMediaSource_InitPipeline( pCtx ) != 0 )
    {
        LogError( ( "Failed to initialize pipeline" ) );
        pthread_mutex_destroy( &pCtx->mediaMutex );
        return -1;
    }

    // Ensure pipeline is in PLAYING state
    if( !ensure_pipeline_playing( &pCtx->videoContext ) )
    {
        LogError( ( "Failed to start pipeline" ) );
        GstMediaSource_Cleanup( pCtx );
        return -1;
    }

    // Create threads for video and audio tasks
    pthread_t videoTid, audioTid;
    pCtx->videoContext.is_running = TRUE;
    pCtx->audioContext.is_running = TRUE;

    if( pthread_create( &videoTid,
                        NULL,
                        VideoTx_Task,
                        &pCtx->videoContext ) != 0 )
    {
        LogError( ( "Failed to create video task" ) );
        GstMediaSource_Cleanup( pCtx );
        return -1;
    }

    if( pthread_create( &audioTid,
                        NULL,
                        AudioTx_Task,
                        &pCtx->audioContext ) != 0 )
    {
        LogError( ( "Failed to create audio task" ) );
        pCtx->videoContext.is_running = FALSE;
        pthread_join( videoTid,
                      NULL );
        GstMediaSource_Cleanup( pCtx );
        return -1;
    }

    // Add a small delay to ensure the pipeline has started
    g_usleep( 100000 );  // 100ms

    LogInfo( ( "GstMediaSource initialized successfully" ) );
    return 0;
}

int32_t GstMediaSource_InitVideoTransceiver( GstMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pVideoTransceiver )
{
    if( !pCtx || !pVideoTransceiver )
    {
        LogError( ( "Invalid input parameters" ) );
        return -1;
    }

    memset( pVideoTransceiver,
            0,
            sizeof( Transceiver_t ) );

    pVideoTransceiver->trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
    pVideoTransceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDRECV;

    TRANSCEIVER_ENABLE_CODEC( pVideoTransceiver->codecBitMap,
                              TRANSCEIVER_RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_BIT );

    pVideoTransceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND;
    pVideoTransceiver->rollingbufferBitRate = DEFAULT_TRANSCEIVER_VIDEO_BIT_RATE;

    strncpy( pVideoTransceiver->streamId,
             DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID,
             sizeof( pVideoTransceiver->streamId ) - 1 );
    pVideoTransceiver->streamIdLength = strlen( pVideoTransceiver->streamId );

    strncpy( pVideoTransceiver->trackId,
             DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID,
             sizeof( pVideoTransceiver->trackId ) - 1 );
    pVideoTransceiver->trackIdLength = strlen( pVideoTransceiver->trackId );

    pVideoTransceiver->onPcEventCallbackFunc = HandlePcEventCallback;
    pVideoTransceiver->pOnPcEventCustomContext = &pCtx->videoContext;

    return 0;
}

int32_t GstMediaSource_InitAudioTransceiver( GstMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pAudioTransceiver )
{
    if( !pCtx || !pAudioTransceiver )
    {
        LogError( ( "Invalid input parameters" ) );
        return -1;
    }

    memset( pAudioTransceiver,
            0,
            sizeof( Transceiver_t ) );

    pAudioTransceiver->trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
    pAudioTransceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDRECV;

    TRANSCEIVER_ENABLE_CODEC( pAudioTransceiver->codecBitMap,
                              TRANSCEIVER_RTC_CODEC_OPUS_BIT );

    pAudioTransceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND;
    pAudioTransceiver->rollingbufferBitRate = DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE;

    strncpy( pAudioTransceiver->streamId,
             DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID,
             sizeof( pAudioTransceiver->streamId ) - 1 );
    pAudioTransceiver->streamIdLength = strlen( pAudioTransceiver->streamId );

    strncpy( pAudioTransceiver->trackId,
             DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID,
             sizeof( pAudioTransceiver->trackId ) - 1 );
    pAudioTransceiver->trackIdLength = strlen( pAudioTransceiver->trackId );

    pAudioTransceiver->onPcEventCallbackFunc = HandlePcEventCallback;
    pAudioTransceiver->pOnPcEventCustomContext = &pCtx->audioContext;

    return 0;
}


void GstMediaSource_Cleanup( GstMediaSourcesContext_t * pCtx )
{
    if( !pCtx )
        return;

    // Stop threads
    pCtx->videoContext.is_running = FALSE;
    pCtx->audioContext.is_running = FALSE;

    // Stop main loops if they exist
    if (pCtx->videoContext.main_loop) {
        g_main_loop_quit(pCtx->videoContext.main_loop);
    }
    if (pCtx->audioContext.main_loop) {
        g_main_loop_quit(pCtx->audioContext.main_loop);
    }

    // Give threads time to exit
    g_usleep( 100000 );  // 100ms

    // Stop pipeline
    if( pCtx->videoContext.pipeline )
    {
        gst_element_set_state( pCtx->videoContext.pipeline,
                               GST_STATE_NULL );
        gst_object_unref( pCtx->videoContext.pipeline );
    }

    // Clean up sinks
    if( pCtx->videoContext.appsink )
    {
        gst_object_unref( pCtx->videoContext.appsink );
    }
    if( pCtx->audioContext.appsink )
    {
        gst_object_unref( pCtx->audioContext.appsink );
    }

    // Clean up encoders
    if( pCtx->videoContext.encoder )
    {
        gst_object_unref( pCtx->videoContext.encoder );
    }

    // Clean up mutex
    pthread_mutex_destroy( &pCtx->mediaMutex );

    LogInfo( ( "GstMediaSource cleaned up" ) );
}
