/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

static int32_t OnNewVideoSample( GstElement * sink,
                                 gpointer user_data )
{
    int32_t ret = 0;

    GstMediaSourceContext_t * pVideoContext = ( GstMediaSourceContext_t * )user_data;
    MediaFrame_t frame;
    GstBuffer * pBuffer;
    GstMapInfo map;
    GstSample * pSample;

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
        if( pVideoContext->numReadyPeer == 0 )
        {
            LogError( ( "No ready peer for video" ) );
            ret = -1;;
        }
    }

    if( ret == 0 )
    {
        #if ENABLE_TWCC_SUPPORT
            if( pVideoContext->pSourcesContext->onBitrateModifier )
            {
                pVideoContext->pSourcesContext->onBitrateModifier(
                    pVideoContext->pSourcesContext->pBitrateModifierCustomContext,
                    pVideoContext->pEncoder );
            }
        #endif /* ENABLE_TWCC_SUPPORT */

        pSample = gst_app_sink_pull_sample( GST_APP_SINK( sink ) );
        if( NULL == pSample )
        {
            ret = -1;;
        }
    }

    if( ret == 0 )
    {
        pBuffer = gst_sample_get_buffer( pSample );

        if( gst_buffer_map( pBuffer,
                            &map,
                            GST_MAP_READ ) )
        {
            frame.pData = map.data;
            frame.size = map.size;
            frame.freeData = 0;
            frame.trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
            frame.timestampUs = GST_BUFFER_PTS( pBuffer ) / 1000;

            if( pVideoContext->pSourcesContext->onMediaSinkHookFunc )
            {
                LogVerbose( ( "Sending video frame: size=%u, ts=%lu",
                              frame.size, frame.timestampUs ) );
                ( void )pVideoContext->pSourcesContext->onMediaSinkHookFunc(
                    pVideoContext->pSourcesContext->pOnMediaSinkHookCustom,
                    &frame );
            }

            gst_buffer_unmap( pBuffer,
                              &map );
        }
        gst_sample_unref( pSample );
    }

    return ret;
}

static void * VideoTx_Task( void * pParameter )
{
    int32_t ret = 0;
    LogDebug( ( "VideoTx_Task started" ) );
    GstMediaSourceContext_t * pVideoContext = ( GstMediaSourceContext_t * )pParameter;
    GMainLoop * pLoop = g_main_loop_new( NULL, FALSE );

    if( pVideoContext == NULL )
    {
        LogError( ( "Invalid audio context" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        // Connect to new-sample signal
        g_signal_connect( pVideoContext->pAppsink,
                          "new-sample",
                          G_CALLBACK( OnNewVideoSample ),
                          pVideoContext );

        pVideoContext->pMainLoop = pLoop;

        // Run the main loop
        g_main_loop_run( pLoop );

        g_main_loop_unref( pLoop );
    }
    LogDebug( ( "VideoTx_Task ending" ) );

    return 0;
}

static int32_t OnNewAudioSample( GstElement * sink,
                                 gpointer user_data )
{
    int32_t ret = 0;

    GstMediaSourceContext_t * pAudioContext = ( GstMediaSourceContext_t * )user_data;
    MediaFrame_t frame;
    GstBuffer * pBuffer;
    GstMapInfo map;
    GstSample * pSample;

    if( NULL == pAudioContext )
    {
        LogError( ( "Invalid audio context" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        if( pAudioContext->numReadyPeer == 0 )
        {
            LogError( ( "No ready peer for audio" ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        #if ENABLE_TWCC_SUPPORT
            if( pAudioContext->pSourcesContext->onBitrateModifier )
            {
                pAudioContext->pSourcesContext->onBitrateModifier(
                    pAudioContext->pSourcesContext->pBitrateModifierCustomContext,
                    pAudioContext->pEncoder );
            }
        #endif /* ENABLE_TWCC_SUPPORT */

        pSample = gst_app_sink_pull_sample( GST_APP_SINK( sink ) );
        if( pSample == NULL )
        {
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        pBuffer = gst_sample_get_buffer( pSample );

        if( gst_buffer_map( pBuffer,
                            &map,
                            GST_MAP_READ ) )
        {
            frame.pData = map.data;
            frame.size = map.size;
            frame.freeData = 0;
            frame.trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
            frame.timestampUs = GST_BUFFER_PTS( pBuffer ) / 1000;

            if( pAudioContext->pSourcesContext->onMediaSinkHookFunc )
            {
                LogVerbose( ( "Sending audio frame: size=%u, ts=%lu",
                              frame.size, frame.timestampUs ) );
                ( void )pAudioContext->pSourcesContext->onMediaSinkHookFunc(
                    pAudioContext->pSourcesContext->pOnMediaSinkHookCustom,
                    &frame );
            }

            gst_buffer_unmap( pBuffer,
                              &map );
        }
        gst_sample_unref( pSample );
    }

    return ret;
}

static void * AudioTx_Task( void * pParameter )
{
    LogDebug( ( "AudioTx_Task started" ) );
    GstMediaSourceContext_t * pAudioContext = ( GstMediaSourceContext_t * )pParameter;

    int32_t ret = 0;

    if( pAudioContext == NULL )
    {
        LogError( ( "Invalid audio context" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        // Connect to new-sample signal
        g_signal_connect( pAudioContext->pAppsink,
                          "new-sample",
                          G_CALLBACK( OnNewAudioSample ),
                          pAudioContext );

        // Create main loop
        GMainLoop * pLoop = g_main_loop_new( NULL,
                                             FALSE );
        pAudioContext->pMainLoop = pLoop;

        // Run the main loop
        g_main_loop_run( pLoop );

        g_main_loop_unref( pLoop );
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
    GstStateChangeReturn change_state_ret;

    if( pMediaSource == NULL )
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



                           change_state_ret = gst_element_set_state( pMediaSource->pSourcesContext->videoContext.pPipeline,
                                             GST_STATE_PLAYING );
                if( change_state_ret == GST_STATE_CHANGE_FAILURE )
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
                if( pMediaSource->numReadyPeer == 0 )
                {
                    // Stop the pipeline if no peers are connected
                    change_state_ret = gst_element_set_state( pMediaSource->pSourcesContext->videoContext.pPipeline,
                                                              GST_STATE_NULL );
                    if( change_state_ret == GST_STATE_CHANGE_FAILURE )
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

    if( NULL == pCtx )
    {
        LogError( ( "Invalid input, pCtx: %p", pCtx ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        gchar * pPipeline_desc = g_strdup_printf(
            "autovideosrc ! videoconvert ! "
            "x264enc name=videoEncoder "
            "tune=zerolatency speed-preset=veryfast "
            "key-int-max=30 bitrate=2000 bframes=0 ref=1 "
            "byte-stream=true aud=false insert-vui=true ! "
            "video/x-h264,profile=constrained-baseline,stream-format=byte-stream,alignment=au ! "
            "h264parse config-interval=1 ! "
            "queue max-size-buffers=2 ! "
            "appsink name=vsink sync=true emit-signals=true max-buffers=1 drop=true "
            #if GSTREAMER_TESTING
                "audiotestsrc ! "
            #else
                "autoaudiosrc ! "
            #endif
            "queue leaky=2 max-size-buffers=400 ! audioconvert ! audioresample ! opusenc name=audioEncoder ! "
            "audio/x-opus,rate=48000,channels=2 ! appsink sync=TRUE emit-signals=TRUE max-buffers=1 drop=true name=asink " );

        GError * pError = NULL;
        pCtx->videoContext.pPipeline = gst_parse_launch( pPipeline_desc,
                                                         &pError );
        g_free( pPipeline_desc );

        if( ( pCtx->videoContext.pPipeline == NULL ) ||
            ( pError != NULL ) )
        {
            LogError( ( "Failed to create pPipeline: %s", pError->message ) );
            g_error_free( pError );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        // Get video sink
        pCtx->videoContext.pAppsink = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pPipeline ),
                                                           "vsink" );
        if( pCtx->videoContext.pAppsink == NULL )
        {
            LogError( ( "Failed to get video appsink" ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        // Get audio sink
        pCtx->audioContext.pAppsink = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pPipeline ),
                                                           "asink" );
        if( pCtx->audioContext.pAppsink == NULL )
        {
            LogError( ( "Failed to get audio appsink" ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        // Share the pPipeline between video and audio contexts
        pCtx->audioContext.pPipeline = pCtx->videoContext.pPipeline;

        // Get encoder elements for bitrate control
        pCtx->audioContext.pEncoder = gst_bin_get_by_name( GST_BIN( pCtx->audioContext.pPipeline ),
                                                           "audioEncoder" );
        if( pCtx->audioContext.pEncoder == NULL )
        {
            LogError( ( "Failed to get audio encoder element" ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        // Get encoder elements for bitrate control
        pCtx->videoContext.pEncoder = gst_bin_get_by_name( GST_BIN( pCtx->videoContext.pPipeline ),
                                                           "videoEncoder" );
        if( pCtx->videoContext.pEncoder == NULL )
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
    if( NULL == pCtx )
    {
        LogError( ( "Invalid input, pCtx: %p", pCtx ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        // Stop main loops if they exist
        if( pCtx->videoContext.pMainLoop )
        {
            g_main_loop_quit( pCtx->videoContext.pMainLoop );
        }

        if( pCtx->audioContext.pMainLoop )
        {
            g_main_loop_quit( pCtx->audioContext.pMainLoop );
        }

        // Stop pipelines (audioContext and videoContext share the same pipeline)
        if( pCtx->videoContext.pPipeline )
        {
            gst_element_set_state( pCtx->videoContext.pPipeline,
                                   GST_STATE_NULL );
            gst_object_unref( pCtx->videoContext.pPipeline );
        }

        // Clean up sinks
        if( pCtx->videoContext.pAppsink )
        {
            gst_object_unref( pCtx->videoContext.pAppsink );
        }

        if( pCtx->audioContext.pAppsink )
        {
            gst_object_unref( pCtx->audioContext.pAppsink );
        }

        // Clean up encoders
        if( pCtx->videoContext.pEncoder )
        {
            gst_object_unref( pCtx->videoContext.pEncoder );
        }

        if( pCtx->audioContext.pEncoder )
        {
            gst_object_unref( pCtx->audioContext.pEncoder );
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

        g_object_get( G_OBJECT( pCtx->videoContext.pEncoder ),
                      "bitrate",
                      &bitrate,
                      NULL );
        // Convert from kbps to bps
        pVideoTransceiver->rollingbufferBitRate = bitrate * 1024;

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
        memset( pAudioTransceiver,
                0,
                sizeof( Transceiver_t ) );

        pAudioTransceiver->trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
        pAudioTransceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDONLY;

        TRANSCEIVER_ENABLE_CODEC( pAudioTransceiver->codecBitMap,
                                  TRANSCEIVER_RTC_CODEC_OPUS_BIT );

        pAudioTransceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURATION_SECOND;

        g_object_get( G_OBJECT( pCtx->audioContext.pEncoder ),
                      "bitrate",
                      &bitrate,
                      NULL );
        // Convert from kbps to bps
        pAudioTransceiver->rollingbufferBitRate = bitrate * 1024;

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
