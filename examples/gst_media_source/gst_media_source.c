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
#include "demo_config.h"

#define DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND ( 3 )

#define DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID "myKvsVideoStream"
#define DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID "myVideoTrack"
#define DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID "myAudioTrack"

static int32_t on_new_video_sample( GstElement * sink,
                                    gpointer user_data )
{
    int32_t ret = 0;

    GstMediaSourceContext_t * pVideoContext = ( GstMediaSourceContext_t * )user_data;
    WebrtcFrame_t frame;
    GstBuffer * buffer;
    GstMapInfo map;
    GstSample * sample;

    if( ret == 0 )
    {
        if( NULL == pVideoContext )
        {
            LogError( ( "Invalid video context" ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        if( 0 == pVideoContext->numReadyPeer )
        {
            LogError( ( "No ready peer for video" ) );
            ret = -1;;
        }
    }

    if( ret == 0 )
    {
        sample = gst_app_sink_pull_sample( GST_APP_SINK( sink ) );
        if( NULL == sample )
        {
            ret = -1;;
        }
    }

    if( ret == 0 )
    {
        buffer = gst_sample_get_buffer( sample );

        if( gst_buffer_map( buffer,
                            &map,
                            GST_MAP_READ ) )
        {
            frame.pData = map.data;
            frame.size = map.size;
            frame.freeData = 0;
            frame.trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
            frame.timestampUs = GST_BUFFER_PTS( buffer ) / 1000;

            if( pVideoContext->pSourcesContext->onMediaSinkHookFunc )
            {
                LogVerbose( ( "Sending video frame: size=%zu, ts=%lu",
                              frame.size, frame.timestampUs ) );
                ( void )pVideoContext->pSourcesContext->onMediaSinkHookFunc(
                    pVideoContext->pSourcesContext->pOnMediaSinkHookCustom,
                    &frame );
            }

            gst_buffer_unmap( buffer,
                              &map );
        }
        gst_sample_unref( sample );
    }

    return ret;
}

static void * VideoTx_Task( void * pParameter )
{
    int32_t ret = 0;
    LogDebug( ( "VideoTx_Task started" ) );
    GstMediaSourceContext_t * pVideoContext = ( GstMediaSourceContext_t * )pParameter;

    if( !pVideoContext )
    {
        LogError( ( "Invalid audio context" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        // Connect to new-sample signal
        g_signal_connect( pVideoContext->appsink,
                          "new-sample",
                          G_CALLBACK( on_new_video_sample ),
                          pVideoContext );

        // Create main loop
        GMainLoop * loop = g_main_loop_new( NULL,
                                            FALSE );
        pVideoContext->main_loop = loop;

        // Run the main loop
        g_main_loop_run( loop );

        g_main_loop_unref( loop );
    }
    LogDebug( ( "VideoTx_Task ending" ) );

    return 0;
}

static int32_t on_new_audio_sample( GstElement * sink,
                                 gpointer user_data )
{
    int32_t ret = 0;

    GstMediaSourceContext_t * pAudioContext = ( GstMediaSourceContext_t * )user_data;
    WebrtcFrame_t frame;
    GstBuffer * buffer;
    GstMapInfo map;
    GstSample * sample;

    if( NULL == pAudioContext )
    {
        LogError( ( "Invalid audio context" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        if( 0 == pAudioContext->numReadyPeer )
        {
            LogError( ( "No ready peer for audio" ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        sample = gst_app_sink_pull_sample( GST_APP_SINK( sink ) );
        if( !sample )
        {
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        buffer = gst_sample_get_buffer( sample );

        if( gst_buffer_map( buffer,
                            &map,
                            GST_MAP_READ ) )
        {
            frame.pData = map.data;
            frame.size = map.size;
            frame.freeData = 0;
            frame.trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
            frame.timestampUs = GST_BUFFER_PTS( buffer ) / 1000;

            if( pAudioContext->pSourcesContext->onMediaSinkHookFunc )
            {
                LogVerbose( ( "Sending audio frame: size=%zu, ts=%lu",
                            frame.size, frame.timestampUs ) );
                ( void )pAudioContext->pSourcesContext->onMediaSinkHookFunc(
                    pAudioContext->pSourcesContext->pOnMediaSinkHookCustom,
                    &frame );
            }

            gst_buffer_unmap( buffer,
                              &map );
        }
        gst_sample_unref( sample );
    }

    return ret;
}

static void * AudioTx_Task( void * pParameter )
{
    LogDebug( ( "AudioTx_Task started" ) );
    GstMediaSourceContext_t * pAudioContext = ( GstMediaSourceContext_t * )pParameter;

    int32_t ret = 0;

    if( !pAudioContext )
    {
        LogError( ( "Invalid audio context" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        // Connect to new-sample signal
        g_signal_connect( pAudioContext->appsink,
                          "new-sample",
                          G_CALLBACK( on_new_audio_sample ),
                          pAudioContext );

        // Create main loop
        GMainLoop * loop = g_main_loop_new( NULL,
                                            FALSE );
        pAudioContext->main_loop = loop;

        // Run the main loop
        g_main_loop_run( loop );

        g_main_loop_unref( loop );
    }
    LogDebug( ( "AudioTx_Task ending" ) );

    return 0;
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
        ret = -1;
    }

    if( ret == 0 )
    {
        switch( event )
        {
            case TRANSCEIVER_CB_EVENT_REMOTE_PEER_READY:
                pthread_mutex_lock( &pMediaSource->pSourcesContext->mediaMutex );
                pMediaSource->numReadyPeer++;
                pthread_mutex_unlock( &pMediaSource->pSourcesContext->mediaMutex );
                LogInfo( ( "Remote peer ready for track kind %d, peers: %d",
                           pMediaSource->trackKind, pMediaSource->numReadyPeer ) );


                GstStateChangeReturn ret;
                ret = gst_element_set_state( pMediaSource->pSourcesContext->videoContext.pipeline,
                                             GST_STATE_PLAYING );
                if( ret == GST_STATE_CHANGE_FAILURE )
                {
                    LogError( ( "Failed to set pipeline to PLAYING state" ) );
                    ret = -1;
                }
                break;

            case TRANSCEIVER_CB_EVENT_REMOTE_PEER_CLOSED:
                pthread_mutex_lock( &pMediaSource->pSourcesContext->mediaMutex );
                if( pMediaSource->numReadyPeer > 0 )
                    pMediaSource->numReadyPeer--;
                pthread_mutex_unlock( &pMediaSource->pSourcesContext->mediaMutex );
                LogInfo( ( "Remote peer closed for track kind %d, peers: %d",
                           pMediaSource->trackKind, pMediaSource->numReadyPeer ) );
                if( 0 == pMediaSource->numReadyPeer )
                {
                    // Stop the pipeline if no peers are connected
                    GstStateChangeReturn ret;
                    ret = gst_element_set_state( pMediaSource->pSourcesContext->videoContext.pipeline,
                                                 GST_STATE_NULL );
                    if( ret == GST_STATE_CHANGE_FAILURE )
                    {
                        LogError( ( "Failed to set pipeline to NULL state" ) );
                        ret = -1;
                    }
                }
                break;

            default:
                LogWarn( ( "Unknown event: %d", event ) );
                ret = -1;
                break;
        }
    }

    return ret;
}

static int32_t InitPipeline( GstMediaSourcesContext_t * pCtx )
{
    int32_t ret = 0;

    if( ( NULL == pCtx ) )
    {
        LogError( ( "Invalid input, pCtx: %p", pCtx ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        gchar * pipeline_desc = g_strdup_printf(
            "autovideosrc ! videoconvert ! "
            "x264enc name=videoEncoder "
            "tune=zerolatency speed-preset=veryfast "
            "key-int-max=30 bitrate=2000 bframes=0 ref=1 "
            "byte-stream=true aud=false insert-vui=true ! "
            "video/x-h264,profile=constrained-baseline,stream-format=byte-stream,alignment=au ! "
            "h264parse config-interval=1 ! "
            "queue max-size-buffers=2 ! "
            "appsink name=vsink sync=true emit-signals=true max-buffers=1 drop=true "
            "autoaudiosrc ! "
            "queue leaky=2 max-size-buffers=400 ! audioconvert ! audioresample ! opusenc name=audioEncoder ! "
            "audio/x-opus,rate=48000,channels=2 ! appsink sync=TRUE emit-signals=TRUE max-buffers=1 drop=true name=asink " );

        GError * error = NULL;
        pCtx->videoContext.pipeline = gst_parse_launch( pipeline_desc,
                                                        &error );
        g_free( pipeline_desc );

        if( !pCtx->videoContext.pipeline )
        {
            LogError( ( "Failed to create pipeline: %s", error->message ) );
            g_error_free( error );
            ret = -1;
        }

        // Get video sink
        pCtx->videoContext.appsink = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pipeline ),
                                                          "vsink" );
        if( !pCtx->videoContext.appsink )
        {
            LogError( ( "Failed to get video appsink" ) );
            ret = -1;
        }

        // Get audio sink
        pCtx->audioContext.appsink = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pipeline ),
                                                          "asink" );
        if( !pCtx->audioContext.appsink )
        {
            LogError( ( "Failed to get audio appsink" ) );
            ret = -1;
        }

        // Share the pipeline between video and audio contexts
        pCtx->audioContext.pipeline = pCtx->videoContext.pipeline;

        // Get encoder elements for bitrate control
        pCtx->audioContext.encoder = gst_bin_get_by_name( GST_BIN( pCtx->audioContext.pipeline ),
                                                          "audioEncoder" );
        if( !pCtx->audioContext.encoder )
        {
            LogError( ( "Failed to get audio encoder element" ) );
            ret = -1;
        }

        // Get encoder elements for bitrate control
        pCtx->videoContext.encoder = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pipeline ),
                                                          "videoEncoder" );
        if( !pCtx->videoContext.encoder )
        {
            LogError( ( "Failed to get video encoder element" ) );
            ret = -1;
        }
    }
    return ret;
}

int32_t GstMediaSource_Cleanup( GstMediaSourcesContext_t * pCtx )
{
    int32_t ret = 0;
    if( ( NULL == pCtx ) )
    {
        LogError( ( "Invalid input, pCtx: %p", pCtx ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        // Stop main loops if they exist
        if( pCtx->videoContext.main_loop )
        {
            g_main_loop_quit( pCtx->videoContext.main_loop );
        }

        if( pCtx->audioContext.main_loop )
        {
            g_main_loop_quit( pCtx->audioContext.main_loop );
        }

        // Stop pipelines (audioContext and videoContext share the same pipeline)
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

        if( pCtx->audioContext.encoder )
        {
            gst_object_unref( pCtx->audioContext.encoder );
        }

        // Clean up mutex
        pthread_mutex_destroy( &pCtx->mediaMutex );
    }

    return ret;
}

int32_t GstMediaSource_Init( GstMediaSourcesContext_t * pCtx,
                             GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                             void * pOnMediaSinkHookCustom )
{
    int32_t ret = 0;
    pthread_t videoTid, audioTid;

    if( NULL == pCtx )
    {
        LogError( ( "Invalid context" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        gst_init( NULL,
                  NULL );
        memset( pCtx,
                0,
                sizeof( GstMediaSourcesContext_t ) );

        if( pthread_mutex_init( &pCtx->mediaMutex,
                                NULL ) != 0 )
        {
            LogError( ( "Failed to create media mutex" ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        pCtx->onMediaSinkHookFunc = onMediaSinkHookFunc;
        pCtx->pOnMediaSinkHookCustom = pOnMediaSinkHookCustom;

        // Initialize contexts
        pCtx->videoContext.pSourcesContext = pCtx;
        pCtx->audioContext.pSourcesContext = pCtx;

        // Initialize single pipeline
        if( InitPipeline( pCtx ) != 0 )
        {
            LogError( ( "Failed to initialize pipeline" ) );
            pthread_mutex_destroy( &pCtx->mediaMutex );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        if( pthread_create( &videoTid,
                            NULL,
                            VideoTx_Task,
                            &pCtx->videoContext ) != 0 )
        {
            LogError( ( "Failed to create video task" ) );
            GstMediaSource_Cleanup( pCtx );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        if( pthread_create( &audioTid,
                            NULL,
                            AudioTx_Task,
                            &pCtx->audioContext ) != 0 )
        {
            LogError( ( "Failed to create audio task" ) );
            pthread_join( videoTid,
                          NULL );
            GstMediaSource_Cleanup( pCtx );
            ret = -1;
        }

        LogInfo( ( "GstMediaSource initialized successfully" ) );
    }

    return ret;
}

int32_t GstMediaSource_InitVideoTransceiver( GstMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pVideoTransceiver )
{
    int32_t ret = 0;
    u_int32_t bitrate;

    if( ( pCtx == NULL ) || ( pVideoTransceiver == NULL ) )
    {
        LogError( ( "Invalid input parameters" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        memset( pVideoTransceiver,
                0,
                sizeof( Transceiver_t ) );

        pVideoTransceiver->trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
        pVideoTransceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDONLY;

        TRANSCEIVER_ENABLE_CODEC( pVideoTransceiver->codecBitMap,
                                  TRANSCEIVER_RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_BIT );

        pVideoTransceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND;

        g_object_get( G_OBJECT( pCtx->videoContext.encoder ),
                      "bitrate",
                      &bitrate,
                      NULL );
        // Convert from kbps to bps
        pVideoTransceiver->rollingbufferBitRate = bitrate * 1000;

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
    }

    return ret;
}

int32_t GstMediaSource_InitAudioTransceiver( GstMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pAudioTransceiver )
{
    int32_t ret = 0;
    uint32_t bitrate;

    if( ( pCtx == NULL ) || ( pAudioTransceiver == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pAudioTransceiver: %p", pCtx, pAudioTransceiver ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        LogDebug( "Initialize audio transceiver" );
        memset( pAudioTransceiver,
                0,
                sizeof( Transceiver_t ) );

        pAudioTransceiver->trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
        pAudioTransceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDONLY;

        TRANSCEIVER_ENABLE_CODEC( pAudioTransceiver->codecBitMap,
                                  TRANSCEIVER_RTC_CODEC_OPUS_BIT );

        pAudioTransceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND;

        g_object_get( G_OBJECT( pCtx->audioContext.encoder ),
                      "bitrate",
                      &bitrate,
                      NULL );
        pAudioTransceiver->rollingbufferBitRate = bitrate;

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
    }

    return ret;
}
