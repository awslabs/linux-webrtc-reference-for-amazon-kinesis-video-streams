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
#define DEFAULT_TRANSCEIVER_AUDIO_BIT_RATE ( 510 * 1024 )

#define DEFAULT_TRANSCEIVER_MEDIA_STREAM_ID "myKvsVideoStream"
#define DEFAULT_TRANSCEIVER_VIDEO_TRACK_ID "myVideoTrack"
#define DEFAULT_TRANSCEIVER_AUDIO_TRACK_ID "myAudioTrack"

#define DEMO_TRANSCEIVER_VIDEO_DATA_QUEUE_NAME "/TxVideoMq"
#define DEMO_TRANSCEIVER_AUDIO_DATA_QUEUE_NAME "/TxAudioMq"
#define DEMO_TRANSCEIVER_MAX_QUEUE_MSG_NUM ( 10 )

static void VideoTx_Task( void * pParameter );
static void AudioTx_Task( void * pParameter );

static void VideoTx_Task( void * pParameter )
{
    AppMediaSourceContext_t * pVideoContext = ( AppMediaSourceContext_t * )pParameter;
    MessageQueueResult_t retMessageQueue;
    uint8_t skipProcess = 0;
    webrtc_frame_t frame;
    size_t frameLength;

    if( pVideoContext == NULL )
    {
        LogError( ( "Invalid input, pVideoContext: %p", pVideoContext ) );
        skipProcess = 1;
    }

    /* Handle event. */
    while( skipProcess == 0 )
    {
        /* Recevied message from data queue. */
        frameLength = sizeof( webrtc_frame_t );
        retMessageQueue = MessageQueue_Recv( &pVideoContext->dataQueue,
                                             &frame,
                                             &frameLength );
        if( retMessageQueue == MESSAGE_QUEUE_RESULT_OK )
        {
            /* Received a media frame. */
            LogVerbose( ( "Video Tx frame(%ld), trackKind: %d, timestamp: %llu, payload: 0x%x 0x%x 0x%x 0x%x", frame.size, frame.trackKind, frame.timestampUs, frame.pData[0], frame.pData[1], frame.pData[2], frame.pData[3] ) );
            if( pVideoContext->pSourcesContext->onMediaSinkHookFunc )
            {
                ( void ) pVideoContext->pSourcesContext->onMediaSinkHookFunc( pVideoContext->pSourcesContext->pOnMediaSinkHookCustom, &frame );
            }

            if( frame.freeData )
            {
                free( frame.pData );
            }
        }
    }

    for( ;; )
    {
        usleep( 200 * 1000 );
        //vTaskDelay( pdMS_TO_TICKS( 200 ) );
    }
}

static void AudioTx_Task( void * pParameter )
{
    AppMediaSourceContext_t * pAudioContext = ( AppMediaSourceContext_t * )pParameter;
    MessageQueueResult_t retMessageQueue;
    uint8_t skipProcess = 0;
    webrtc_frame_t frame;
    size_t frameLength;

    if( pAudioContext == NULL )
    {
        LogError( ( "Invalid input, pAudioContext: %p", pAudioContext ) );
        skipProcess = 1;
    }

    /* Handle event. */
    while( skipProcess == 0 )
    {
        /* Recevied message from data queue. */
        frameLength = sizeof( webrtc_frame_t );
        retMessageQueue = MessageQueue_Recv( &pAudioContext->dataQueue,
                                             &frame,
                                             &frameLength );
        if( retMessageQueue == MESSAGE_QUEUE_RESULT_OK )
        {
            /* Received a media frame. */
            LogVerbose( ( "Audio Tx frame(%ld), track kind: %d, timestampUs: %llu", frame.size, frame.trackKind, frame.timestampUs ) );

            if( frame.freeData )
            {
                free( frame.pData );
            }
        }
    }

    for( ;; )
    {
        usleep( 200 * 1000 );
        //vTaskDelay( pdMS_TO_TICKS( 200 ) );
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
        /* Start media transmission. */
        ret = AppMediaSourcePort_Start();
        if( ret == 0 )
        {
            pMediaSource->pSourcesContext->isPortStarted = 1;
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
        /* Create task for video Tx. */
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
        /* Create task for audio Tx. */
        // if( xTaskCreate( AudioTx_Task, ( ( const char * )"AudioTask" ), 2048, pAudioSource, tskIDLE_PRIORITY + 1, NULL ) != pdPASS )
        // {
        //     LogError( ( "xTaskCreate(AudioTask) failed" ) );
        //     ret = -1;
        // }
    }

    return ret;
}

static int32_t OnFrameReadyToSend( void * pCtx,
                                   webrtc_frame_t * pFrame )
{
    int32_t ret = 0;
    AppMediaSourceContext_t * pMediaSource = ( AppMediaSourceContext_t * )pCtx;
    MessageQueueResult_t retMessageQueue;
    webrtc_frame_t dropFrame;
    size_t dropFrameSize;

    if( ( pCtx == NULL ) || ( pFrame == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: %p, pFrame: %p", pCtx, pFrame ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        retMessageQueue = MessageQueue_IsFull( &pMediaSource->dataQueue );
        /* Drop oldest packet if full. */
        if( retMessageQueue == MESSAGE_QUEUE_RESULT_MQ_IS_FULL )
        {
            dropFrameSize = sizeof( webrtc_frame_t );
            ( void ) MessageQueue_Recv( &pMediaSource->dataQueue, &dropFrame, &dropFrameSize );

            if( dropFrame.freeData )
            {
                free( dropFrame.pData );
            }
        }
    }

    if( ret == 0 )
    {
        retMessageQueue = MessageQueue_Send( &pMediaSource->dataQueue,
                                             pFrame,
                                             sizeof( webrtc_frame_t ) );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to send frame ready message to queue, error: %d", retMessageQueue ) );
            ret = -1;
        }
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

    if( ret == 0 )
    {
        ret = AppMediaSourcePort_Init( OnFrameReadyToSend, &pCtx->videoContext, OnFrameReadyToSend, &pCtx->audioContext );
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
