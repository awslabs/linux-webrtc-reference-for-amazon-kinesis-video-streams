#ifndef APP_MEDIA_SOURCE_H
#define APP_MEDIA_SOURCE_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include "message_queue.h"
#include "peer_connection.h"

typedef struct {
    uint8_t * pData;
    uint32_t size;
    uint64_t timestampUs;
    TransceiverTrackKind_t trackKind;
    uint8_t freeData;  /* indicate user need to free pData after using it */
} webrtc_frame_t;

typedef struct AppMediaSourcesContext AppMediaSourcesContext_t;
typedef int32_t (* AppMediaSourceOnMediaSinkHook)( void * pCustom,
                                                   webrtc_frame_t * pFrame );

typedef struct AppMediaSourceContext
{
    /* Mutex to protect numReadyPeer because we might receive multiple ready/close message from different tasks. */
    pthread_mutex_t mediaMutex;

    MessageQueueHandler_t dataQueue;
    uint8_t numReadyPeer;
    TransceiverTrackKind_t trackKind;

    AppMediaSourcesContext_t * pSourcesContext;
} AppMediaSourceContext_t;

typedef struct AppMediaSourcesContext
{
    AppMediaSourceContext_t videoContext;
    AppMediaSourceContext_t audioContext;

    AppMediaSourceOnMediaSinkHook onMediaSinkHookFunc;
    void * pOnMediaSinkHookCustom;
} AppMediaSourcesContext_t;

int32_t AppMediaSource_Init( AppMediaSourcesContext_t * pCtx,
                             AppMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                             void * pOnMediaSinkHookCustom );
int32_t AppMediaSource_InitVideoTransceiver( AppMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pVideoTranceiver );
int32_t AppMediaSource_InitAudioTransceiver( AppMediaSourcesContext_t * pCtx,
                                             Transceiver_t * pAudioTranceiver );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* APP_MEDIA_SOURCE_H */
