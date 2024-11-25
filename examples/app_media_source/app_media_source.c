#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

//#include "FreeRTOS.h"
//#include "task.h"
#include "app_media_source.h"

#define DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURACTION_SECOND ( 3 )

// Considering 4 Mbps for 720p (which is what our samples use). This is for H.264.
// The value could be different for other codecs.
#define DEFAULT_TRANSCEIVER_VIDEO_BIT_RATE ( 4 * 1024 * 1024 )

// For opus, the bitrate could be between 6 Kbps to 510 Kbps
#define DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE ( 1000 * 1024 )

#define DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID "myKvsVideoStream"
#define DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID "myVideoTrack"
#define DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID "myAudioTrack"

#define DEMO_TRANSCEIVER_VIDEO_DATA_QUEUE_NAME "/TxVideoMq"
#define DEMO_TRANSCEIVER_AUDIO_DATA_QUEUE_NAME "/TxAudioMq"
#define DEMO_TRANSCEIVER_MAX_QUEUE_MSG_NUM ( 10 )

#define NUMBER_OF_H264_FRAME_SAMPLE_FILES   1500
#define NUMBER_OF_OPUS_FRAME_SAMPLE_FILES   618
#define MAX_PATH_LEN                        255

#define SAMPLE_AUDIO_FRAME_DURATION_IN_US               ( 20 * 1000 )
#define SAMPLE_AUDIO_FRAME_DURATION_IN_HUNDRED_OF_NANOS ( SAMPLE_AUDIO_FRAME_DURATION_IN_US * 10 )

#define SAMPLE_FPS_VALUE                                25
#define SAMPLE_VIDEO_FRAME_DURATION_IN_US               ( ( 1000 * 1000 ) / 25 )
#define SAMPLE_VIDEO_FRAME_DURATION_IN_HUNDRED_OF_NANOS ( SAMPLE_VIDEO_FRAME_DURATION_IN_US * 10 )


static void * VideoTx_Task( void * pParameter );
static void * AudioTx_Task( void * pParameter );

static void * VideoTx_Task( void * pParameter )
{
    AppMediaSourceContext_t * pVideoContext = ( AppMediaSourceContext_t * )pParameter;
    MessageQueueResult_t retMessageQueue;
    webrtc_frame_t frame;
    char filePath[ MAX_PATH_LEN + 1 ];
    FILE* fp = NULL;
    size_t frameLength, fileIndex = 0;

    if( pVideoContext == NULL )
    {
        LogError( ( "Invalid input, pVideoContext: %p", pVideoContext ) );
    }
    else
    {
        frame.timestampUs = 0;

        while( 1 )
        {
        #ifndef ENABLE_STREAMING_LOOPBACK
            if( pVideoContext->pSourcesContext->isPortStarted == 1 )
            {
                fileIndex = fileIndex % NUMBER_OF_H264_FRAME_SAMPLE_FILES + 1;
                snprintf( filePath, MAX_PATH_LEN, "./examples/app_media_source/samples/h264SampleFrames/frame-%04d.h264", fileIndex );

                fp = fopen( filePath, "rb" );

                if( fp == NULL )
                {
                    LogError( ( "Failed to open %s.", filePath ) );
                }
                else
                {
                    fseek( fp, 0, SEEK_END );
                    frameLength = ftell( fp );

                    frame.pData = ( uint8_t * ) malloc( frameLength );
                    frame.size = frameLength;
                    frame.timestampUs += SAMPLE_VIDEO_FRAME_DURATION_IN_HUNDRED_OF_NANOS;
                    frame.trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;

                    fseek( fp, 0, SEEK_SET );
                    fread( frame.pData, frameLength, 1, fp );

                    fclose( fp );

                    LogDebug( ( "Sending video frame of length %lu.", frameLength ) );
                    if( pVideoContext->pSourcesContext->onMediaSinkHookFunc )
                    {
                        ( void ) pVideoContext->pSourcesContext->onMediaSinkHookFunc( pVideoContext->pSourcesContext->pOnMediaSinkHookCustom, &frame );
                    }

                    free( frame.pData );
                }
            }
        #endif
            usleep( SAMPLE_AUDIO_FRAME_DURATION_IN_US );
        }
    }
}

static void * AudioTx_Task( void * pParameter )
{
    AppMediaSourceContext_t * pAudioContext = ( AppMediaSourceContext_t * )pParameter;
    MessageQueueResult_t retMessageQueue;
    webrtc_frame_t frame;
    char filePath[ MAX_PATH_LEN + 1 ];
    FILE* fp = NULL;
    size_t frameLength, fileIndex = 0;

    if( pAudioContext == NULL )
    {
        LogError( ( "Invalid input, pAudioContext: %p", pAudioContext ) );
    }
    else
    {
        frame.timestampUs = 0;

        while( 1 )
        {
        #ifndef ENABLE_STREAMING_LOOPBACK
            if( pAudioContext->pSourcesContext->isPortStarted == 1 )
            {
                fileIndex = fileIndex % NUMBER_OF_OPUS_FRAME_SAMPLE_FILES + 1;
                snprintf( filePath, MAX_PATH_LEN, "./examples/app_media_source/samples/opusSampleFrames/sample-%03d.opus", fileIndex );

                fp = fopen( filePath, "rb" );

                if( fp == NULL )
                {
                    LogError( ( "Failed to open %s.", filePath ) );
                }
                else
                {
                    fseek( fp, 0, SEEK_END );
                    frameLength = ftell( fp );

                    frame.pData = ( uint8_t * ) malloc( frameLength );
                    frame.size = frameLength;
                    frame.timestampUs += SAMPLE_AUDIO_FRAME_DURATION_IN_HUNDRED_OF_NANOS;
                    frame.trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;

                    fseek( fp, 0, SEEK_SET );
                    fread( frame.pData, frameLength, 1, fp );

                    fclose( fp );

                    LogDebug( ( "Sending audio frame of length %lu.", frameLength ) );
                    if( pAudioContext->pSourcesContext->onMediaSinkHookFunc )
                    {
                        ( void ) pAudioContext->pSourcesContext->onMediaSinkHookFunc( pAudioContext->pSourcesContext->pOnMediaSinkHookCustom, &frame );
                    }

                    free( frame.pData );
                }
            }
        #endif
            usleep( SAMPLE_AUDIO_FRAME_DURATION_IN_US );
        }
    }
}

static int32_t OnPcEventRemotePeerReady( AppMediaSourceContext_t * pMediaSource )
{
    int32_t ret = 0;

    if( pMediaSource == NULL )
    {
        LogError( ( "Invalid input, pMediaSource: %p", pMediaSource ) );
        ret = -1;
    }

    if( ( ret == 0 ) && ( pMediaSource->pSourcesContext->isPortStarted == 0 ) )
    {
        pMediaSource->pSourcesContext->isPortStarted = 1;
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
                                               sizeof( webrtc_frame_t ),
                                               DEMO_TRANSCEIVER_MAX_QUEUE_MSG_NUM );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to open video transceiver data queue." ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        /* Initialize video transceiver. */
        pVideoSource->transceiver.trackKind = TRANSCEIVER_TRACK_KIND_VIDEO;
        pVideoSource->transceiver.direction = TRANSCEIVER_TRACK_DIRECTION_SENDRECV;
        TRANSCEIVER_ENABLE_CODEC( pVideoSource->transceiver.codecBitMap, TRANSCEIVER_RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_BIT );
        pVideoSource->transceiver.rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURACTION_SECOND;
        pVideoSource->transceiver.rollingbufferBitRate = DEFAULT_TRANSCEIVER_VIDEO_BIT_RATE;
        strncpy( pVideoSource->transceiver.streamId, DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID, sizeof( pVideoSource->transceiver.streamId ) );
        pVideoSource->transceiver.streamIdLength = strlen( DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID );
        strncpy( pVideoSource->transceiver.trackId, DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID, sizeof( pVideoSource->transceiver.trackId ) );
        pVideoSource->transceiver.trackIdLength = strlen( DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID );
        pVideoSource->transceiver.onPcEventCallbackFunc = HandlePcEventCallback;
        pVideoSource->transceiver.pOnPcEventCustomContext = pVideoSource;
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
                                               sizeof( webrtc_frame_t ),
                                               DEMO_TRANSCEIVER_MAX_QUEUE_MSG_NUM );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to open audio transceiver data queue." ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        /* Initialize audio transceiver. */
        pAudioSource->transceiver.trackKind = TRANSCEIVER_TRACK_KIND_AUDIO;
        pAudioSource->transceiver.direction = TRANSCEIVER_TRACK_DIRECTION_SENDRECV;
        TRANSCEIVER_ENABLE_CODEC( pAudioSource->transceiver.codecBitMap, TRANSCEIVER_RTC_CODEC_MULAW_BIT );
        TRANSCEIVER_ENABLE_CODEC( pAudioSource->transceiver.codecBitMap, TRANSCEIVER_RTC_CODEC_OPUS_BIT );
        pAudioSource->transceiver.rollingbufferDurationSec = DEFAULT_TRANSCEIVER_ROLLING_BUFFER_DURACTION_SECOND;
        pAudioSource->transceiver.rollingbufferBitRate = DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE;
        strncpy( pAudioSource->transceiver.streamId, DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID, sizeof( pAudioSource->transceiver.streamId ) );
        pAudioSource->transceiver.streamIdLength = strlen( DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID );
        strncpy( pAudioSource->transceiver.trackId, DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID, sizeof( pAudioSource->transceiver.trackId ) );
        pAudioSource->transceiver.trackIdLength = strlen( DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID );
        pAudioSource->transceiver.onPcEventCallbackFunc = HandlePcEventCallback;
        pAudioSource->transceiver.pOnPcEventCustomContext = pAudioSource;
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
        pCtx->videoContext.pSourcesContext = pCtx;
        ret = InitializeVideoSource( &pCtx->videoContext );
    }

    if( ret == 0 )
    {
        pCtx->audioContext.pSourcesContext = pCtx;
        ret = InitializeAudioSource( &pCtx->audioContext );
    }

    if( ret == 0 )
    {
        // pCtx->videoContext.pSourcesContext = pCtx;
        // pCtx->audioContext.pSourcesContext = pCtx;
        pCtx->onMediaSinkHookFunc = onMediaSinkHookFunc;
        pCtx->pOnMediaSinkHookCustom = pOnMediaSinkHookCustom;
    }

    return ret;
}

int32_t AppMediaSource_GetVideoTransceiver( AppMediaSourcesContext_t * pCtx,
                                            Transceiver_t ** ppVideoTranceiver )
{
    int32_t ret = 0;

    if( ( pCtx == NULL ) || ( ppVideoTranceiver == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, ppVideoTranceiver: %p", pCtx, ppVideoTranceiver ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        *ppVideoTranceiver = &pCtx->videoContext.transceiver;
    }

    return ret;
}

int32_t AppMediaSource_GetAudioTransceiver( AppMediaSourcesContext_t * pCtx,
                                            Transceiver_t ** ppAudioTranceiver )
{
    int32_t ret = 0;

    if( ( pCtx == NULL ) || ( ppAudioTranceiver == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pAudioTranceiver: %p", pCtx, ppAudioTranceiver ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        *ppAudioTranceiver = &pCtx->audioContext.transceiver;
    }

    return ret;
}
