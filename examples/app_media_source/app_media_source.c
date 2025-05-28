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
#include <errno.h>

#include "demo_config.h"
#include "app_media_source.h"

#define DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURACTION_SECOND ( 3 )

/* Considering 1.4 Mbps for 720p (which is what our samples use). This is for H.264.
* For H.265, we're using a lower bit rate of about 462 Kbps.
* The value could be different for other codecs. */
#define DEFAULT_TRANSCEIVER_VIDEO_BIT_RATE ( 1.4 * 1024 * 1024 )
#define TRANSCEIVER_H265_VIDEO_BIT_RATE ( 462 * 1024 )

/* For opus, the bitrate could be between 6 Kbps to 1000 Kbps. */
#define DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE ( 1000 * 1024 )

#define DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID "myKvsVideoStream"
#define DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID "myVideoTrack"
#define DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID "myAudioTrack"

#define DEMO_TRANSCEIVER_VIDEO_DATA_QUEUE_NAME "/TxVideoMq"
#define DEMO_TRANSCEIVER_AUDIO_DATA_QUEUE_NAME "/TxAudioMq"
#define DEMO_TRANSCEIVER_MAX_QUEUE_MSG_NUM ( 10 )

#define NUMBER_OF_H264_FRAME_SAMPLE_FILES   1500
#define NUMBER_OF_H265_FRAME_SAMPLE_FILES   1499
#define NUMBER_OF_OPUS_FRAME_SAMPLE_FILES   618
#define MAX_PATH_LEN                        255

#define SAMPLE_AUDIO_FRAME_DURATION_IN_US               ( 20 * 1000 )

#define SAMPLE_FPS_VALUE                                25
#define SAMPLE_VIDEO_FRAME_DURATION_IN_US               ( ( 1000 * 1000 ) / SAMPLE_FPS_VALUE )

static void * VideoTx_Task( void * pParameter );
static void * AudioTx_Task( void * pParameter );

static void * VideoTx_Task( void * pParameter )
{
    AppMediaSourceContext_t * pVideoContext = ( AppMediaSourceContext_t * )pParameter;
    WebrtcFrame_t frame;
    #ifndef ENABLE_STREAMING_LOOPBACK
        char filePath[ MAX_PATH_LEN + 1 ];
        FILE * fp = NULL;
        size_t frameLength;
        size_t allocatedBufferLength = 0;
    #endif /* ifndef ENABLE_STREAMING_LOOPBACK */

    if( pVideoContext == NULL )
    {
        LogError( ( "Invalid input, pVideoContext: %p", pVideoContext ) );
    }
    else
    {
        frame.timestampUs = 0;
        ( void ) frame;

        while( 1 )
        {
            #ifndef ENABLE_STREAMING_LOOPBACK
                if( pVideoContext->numReadyPeer != 0 )
                {
                    #if USE_VIDEO_CODEC_H265
                    {
                        pVideoContext->fileIndex = pVideoContext->fileIndex % NUMBER_OF_H265_FRAME_SAMPLE_FILES + 1;
                        snprintf( filePath, MAX_PATH_LEN, "./examples/app_media_source/samples/h265SampleFrames/frame-%04d.h265", pVideoContext->fileIndex );
                    }
                    #else
                    {
                        pVideoContext->fileIndex = pVideoContext->fileIndex % NUMBER_OF_H264_FRAME_SAMPLE_FILES + 1;
                        snprintf( filePath, MAX_PATH_LEN, "./examples/app_media_source/samples/h264SampleFrames/frame-%04d.h264", pVideoContext->fileIndex );
                    }
                    #endif

                    fp = fopen( filePath, "rb" );

                    if( fp == NULL )
                    {
                        LogError( ( "Failed to open %s. Error: %s", filePath, strerror( errno ) ) );
                    }
                    else
                    {
                        fseek( fp, 0, SEEK_END );
                        frameLength = ftell( fp );

                        if( frameLength > allocatedBufferLength )
                        {
                            if( allocatedBufferLength != 0 )
                            {
                                free( frame.pData );
                            }
                            frame.pData = ( uint8_t * ) malloc( frameLength );
                            allocatedBufferLength = frameLength;
                        }
                        frame.size = frameLength;
                        frame.timestampUs += SAMPLE_VIDEO_FRAME_DURATION_IN_US;
                        frame.trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;

                        fseek( fp, 0, SEEK_SET );
                        if( fread( frame.pData, frameLength, 1, fp ) == 1 )
                        {
                            LogVerbose( ( "Sending video frame of length %lu.", frameLength ) );
                            if( pVideoContext->pSourcesContext->onMediaSinkHookFunc )
                            {
                                ( void ) pVideoContext->pSourcesContext->onMediaSinkHookFunc( pVideoContext->pSourcesContext->pOnMediaSinkHookCustom, &frame );
                            }
                        }
                        else
                        {
                            LogError( ( "VideoTx_Task: fread failed!" ) );
                        }

                        fclose( fp );
                    }
                }
            #endif /* ifndef ENABLE_STREAMING_LOOPBACK */
            usleep( SAMPLE_VIDEO_FRAME_DURATION_IN_US );
        }
    }

    return 0;
}

static void * AudioTx_Task( void * pParameter )
{
    AppMediaSourceContext_t * pAudioContext = ( AppMediaSourceContext_t * )pParameter;
    WebrtcFrame_t frame;
    #ifndef ENABLE_STREAMING_LOOPBACK
        char filePath[ MAX_PATH_LEN + 1 ];
        FILE * fp = NULL;
        size_t frameLength;
        size_t allocatedBufferLength = 0;
    #endif /* ifndef ENABLE_STREAMING_LOOPBACK */

    if( pAudioContext == NULL )
    {
        LogError( ( "Invalid input, pAudioContext: %p", pAudioContext ) );
    }
    else
    {
        frame.timestampUs = 0;
        ( void ) frame;

        while( 1 )
        {
            #ifndef ENABLE_STREAMING_LOOPBACK
                if( pAudioContext->numReadyPeer != 0 )
                {
                    pAudioContext->fileIndex = pAudioContext->fileIndex % NUMBER_OF_OPUS_FRAME_SAMPLE_FILES + 1;
                    snprintf( filePath, MAX_PATH_LEN, "./examples/app_media_source/samples/opusSampleFrames/sample-%03d.opus", pAudioContext->fileIndex );

                    fp = fopen( filePath, "rb" );

                    if( fp == NULL )
                    {
                        LogError( ( "Failed to open %s.", filePath ) );
                    }
                    else
                    {
                        fseek( fp, 0, SEEK_END );
                        frameLength = ftell( fp );

                        if( frameLength > allocatedBufferLength )
                        {
                            if( allocatedBufferLength != 0 )
                            {
                                free( frame.pData );
                            }
                            frame.pData = ( uint8_t * ) malloc( frameLength );
                            allocatedBufferLength = frameLength;
                        }
                        frame.size = frameLength;
                        frame.timestampUs += SAMPLE_AUDIO_FRAME_DURATION_IN_US;
                        frame.trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;

                        fseek( fp, 0, SEEK_SET );
                        if( fread( frame.pData, frameLength, 1, fp ) == 1 )
                        {
                            LogVerbose( ( "Sending audio frame of length %lu.", frameLength ) );
                            if( pAudioContext->pSourcesContext->onMediaSinkHookFunc )
                            {
                                ( void ) pAudioContext->pSourcesContext->onMediaSinkHookFunc( pAudioContext->pSourcesContext->pOnMediaSinkHookCustom, &frame );
                            }
                        }
                        else
                        {
                            LogError( ( "AudioTx_Task: fread failed!" ) );
                        }

                        fclose( fp );
                    }
                }
            #endif /* ifndef ENABLE_STREAMING_LOOPBACK == 0 */
            usleep( SAMPLE_AUDIO_FRAME_DURATION_IN_US );
        }
    }

    return 0;
}

static int32_t OnPcEventRemotePeerReady( AppMediaSourceContext_t * pMediaSource )
{
    int32_t ret = 0;

    if( pMediaSource == NULL )
    {
        LogError( ( "Invalid input, pMediaSource: %p", pMediaSource ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        if( pthread_mutex_lock( &( pMediaSource->pSourcesContext->mediaMutex ) ) == 0 )
        {
            if( pMediaSource->numReadyPeer < AWS_MAX_VIEWER_NUM )
            {
                pMediaSource->numReadyPeer++;
            }

            /* We have finished accessing the shared resource.  Release the mutex. */
            pthread_mutex_unlock( &( pMediaSource->pSourcesContext->mediaMutex ) );
            LogInfo( ( "Starting track kind(%d) media, value=%u", pMediaSource->trackKind, pMediaSource->numReadyPeer ) );
        }
        else
        {
            LogError( ( "Failed to lock media mutex, track kind=%d.", pMediaSource->trackKind ) );
            ret = -1;
        }
    }

    return ret;
}

static int32_t OnPcEventRemotePeerClosed( AppMediaSourceContext_t * pMediaSource )
{
    int32_t ret = 0;

    if( pMediaSource == NULL )
    {
        LogError( ( "Invalid input, pMediaSource: %p", pMediaSource ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        if( pthread_mutex_lock( &( pMediaSource->pSourcesContext->mediaMutex ) ) == 0 )
        {
            if( pMediaSource->numReadyPeer > 0U )
            {
                pMediaSource->numReadyPeer--;
                if( pMediaSource->numReadyPeer == 0 )
                {
                    pMediaSource->fileIndex = 0;
                }
            }

            /* We have finished accessing the shared resource.  Release the mutex. */
            pthread_mutex_unlock( &( pMediaSource->pSourcesContext->mediaMutex ) );
            LogInfo( ( "Stopping track kind(%d) media, value=%u", pMediaSource->trackKind, pMediaSource->numReadyPeer ) );
        }
        else
        {
            LogError( ( "Failed to lock media mutex, track kind=%d.", pMediaSource->trackKind ) );
            ret = -1;
        }
    }

    return ret;
}

static int32_t HandlePcEventCallback( void * pCustomContext,
                                      TransceiverCallbackEvent_t event,
                                      TransceiverCallbackContent_t * pEventMsg )
{
    int32_t ret = 0;
    AppMediaSourceContext_t * pMediaSource = ( AppMediaSourceContext_t * )pCustomContext;

    if( pMediaSource == NULL )
    {
        LogError( ( "Invalid input, pEventMsg: %p", pEventMsg ) );
        ret = -1;
    }

    switch( event )
    {
        case TRANSCEIVER_CB_EVENT_REMOTE_PEER_READY:
            ret = OnPcEventRemotePeerReady( pMediaSource );
            break;
        case TRANSCEIVER_CB_EVENT_REMOTE_PEER_CLOSED:
            ret = OnPcEventRemotePeerClosed( pMediaSource );
            break;
        default:
            LogWarn( ( "Unknown event: 0x%x", event ) );
            break;
    }

    return ret;
}

static int32_t InitializeVideoSource( AppMediaSourceContext_t * pVideoSource )
{
    int32_t ret = 0;
    MessageQueueResult_t retMessageQueue;

    if( pVideoSource == NULL )
    {
        ret = -1;
        LogError( ( "Invalid input, pVideoSource: %p", pVideoSource ) );
    }

    if( ret == 0 )
    {
        retMessageQueue = MessageQueue_Create( &pVideoSource->dataQueue,
                                               DEMO_TRANSCEIVER_VIDEO_DATA_QUEUE_NAME,
                                               sizeof( WebrtcFrame_t ),
                                               DEMO_TRANSCEIVER_MAX_QUEUE_MSG_NUM );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to open video transceiver data queue." ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        pVideoSource->trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
    }

    if( ret == 0 )
    {
        pthread_t tid;
        /* Create task for video Tx. */
        pthread_create( &tid,
                        NULL,
                        VideoTx_Task,
                        pVideoSource );
        // if( xTaskCreate( VideoTx_Task, ( ( const char * )"VideoTask" ), 2048, pVideoSource, tskIDLE_PRIORITY + 1, NULL ) != pdPASS )
        // {
        //     LogError( ( "xTaskCreate(VideoTask) failed" ) );
        //     ret = -1;
        // }
    }

    return ret;
}

static int32_t InitializeAudioSource( AppMediaSourceContext_t * pAudioSource )
{
    int32_t ret = 0;
    MessageQueueResult_t retMessageQueue;

    if( pAudioSource == NULL )
    {
        ret = -1;
        LogError( ( "Invalid input, pAudioSource: %p", pAudioSource ) );
    }

    if( ret == 0 )
    {
        retMessageQueue = MessageQueue_Create( &pAudioSource->dataQueue,
                                               DEMO_TRANSCEIVER_AUDIO_DATA_QUEUE_NAME,
                                               sizeof( WebrtcFrame_t ),
                                               DEMO_TRANSCEIVER_MAX_QUEUE_MSG_NUM );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to open audio transceiver data queue." ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        pAudioSource->trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
    }

    if( ret == 0 )
    {
        pthread_t tid;
        /* Create task for audio Tx. */
        pthread_create( &tid,
                        NULL,
                        AudioTx_Task,
                        pAudioSource );

        // if( xTaskCreate( AudioTx_Task, ( ( const char * )"AudioTask" ), 2048, pAudioSource, tskIDLE_PRIORITY + 1, NULL ) != pdPASS )
        // {
        //     LogError( ( "xTaskCreate(AudioTask) failed" ) );
        //     ret = -1;
        // }
    }

    return ret;
}

int32_t AppMediaSource_Init( AppMediaSourcesContext_t * pCtx,
                             AppMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                             void * pOnMediaSinkHookCustom )
{
    int32_t ret = 0;

    if( pCtx == NULL )
    {
        LogError( ( "Invalid input, pCtx: %p", pCtx ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        memset( pCtx, 0, sizeof( AppMediaSourcesContext_t ) );

        /* Mutex can only be created in executing scheduler. */
        if( pthread_mutex_init( &( pCtx->mediaMutex ),
                                NULL ) != 0 )
        {
            LogError( ( "Fail to create mutex for media source." ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        ret = InitializeVideoSource( &pCtx->videoContext );
    }

    if( ret == 0 )
    {
        ret = InitializeAudioSource( &pCtx->audioContext );
    }

    if( ret == 0 )
    {
        pCtx->videoContext.pSourcesContext = pCtx;
        pCtx->audioContext.pSourcesContext = pCtx;
        pCtx->onMediaSinkHookFunc = onMediaSinkHookFunc;
        pCtx->pOnMediaSinkHookCustom = pOnMediaSinkHookCustom;
    }

    return ret;
}

int32_t AppMediaSource_InitVideoTransceiver( AppMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pVideoTranceiver )
{
    int32_t ret = 0;

    if( ( pCtx == NULL ) || ( pVideoTranceiver == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pVideoTranceiver: %p", pCtx, pVideoTranceiver ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        /* Initialize video transceiver. */
        memset( pVideoTranceiver, 0, sizeof( Transceiver_t ) );
        pVideoTranceiver->trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
        pVideoTranceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDRECV;
        #if USE_VIDEO_CODEC_H265
        {
            TRANSCEIVER_ENABLE_CODEC( pVideoTranceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_H265_BIT );
            pVideoTranceiver->rollingbufferBitRate = TRANSCEIVER_H265_VIDEO_BIT_RATE;
        }
        #else
        {
            TRANSCEIVER_ENABLE_CODEC( pVideoTranceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_BIT );
            pVideoTranceiver->rollingbufferBitRate = DEFAULT_TRANSCEIVER_VIDEO_BIT_RATE;
        }
        #endif
        pVideoTranceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURACTION_SECOND;
        strncpy( pVideoTranceiver->streamId, DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID, sizeof( pVideoTranceiver->streamId ) );
        pVideoTranceiver->streamIdLength = strlen( DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID );
        strncpy( pVideoTranceiver->trackId, DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID, sizeof( pVideoTranceiver->trackId ) );
        pVideoTranceiver->trackIdLength = strlen( DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID );
        pVideoTranceiver->onPcEventCallbackFunc = HandlePcEventCallback;
        pVideoTranceiver->pOnPcEventCustomContext = &pCtx->videoContext;
    }

    return ret;
}

int32_t AppMediaSource_InitAudioTransceiver( AppMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pAudioTranceiver )
{
    int32_t ret = 0;

    if( ( pCtx == NULL ) || ( pAudioTranceiver == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pAudioTranceiver: %p", pCtx, pAudioTranceiver ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        /* Initialize audio transceiver. */
        memset( pAudioTranceiver, 0, sizeof( Transceiver_t ) );
        pAudioTranceiver->trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
        pAudioTranceiver->direction = TRANSCEIVER_TRACK_DIRECTION_SENDRECV;
        #if ( AUDIO_OPUS )
            TRANSCEIVER_ENABLE_CODEC( pAudioTranceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_OPUS_BIT );
        #else
            TRANSCEIVER_ENABLE_CODEC( pAudioTranceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_MULAW_BIT );
            TRANSCEIVER_ENABLE_CODEC( pAudioTranceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_ALAW_BIT );
        #endif
        pAudioTranceiver->rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURACTION_SECOND;
        pAudioTranceiver->rollingbufferBitRate = DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE;
        strncpy( pAudioTranceiver->streamId, DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID, sizeof( pAudioTranceiver->streamId ) );
        pAudioTranceiver->streamIdLength = strlen( DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID );
        strncpy( pAudioTranceiver->trackId, DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID, sizeof( pAudioTranceiver->trackId ) );
        pAudioTranceiver->trackIdLength = strlen( DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID );
        pAudioTranceiver->onPcEventCallbackFunc = HandlePcEventCallback;
        pAudioTranceiver->pOnPcEventCustomContext = &pCtx->audioContext;
    }

    return ret;
}
