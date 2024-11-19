#ifndef APP_MEDIA_SOURCE_H
#define APP_MEDIA_SOURCE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "message_queue.h"
#include "peer_connection.h"
#include "app_media_source_port.h"

typedef struct AppMediaSourcesContext AppMediaSourcesContext_t;
typedef int32_t (* AppMediaSourceOnMediaSinkHook)( void * pCustom,
                                                   webrtc_frame_t * pFrame );

typedef struct AppMediaSourceContext
{
    MessageQueueHandler_t dataQueue;
    Transceiver_t transceiver;

    AppMediaSourcesContext_t * pSourcesContext;
} AppMediaSourceContext_t;

typedef struct AppMediaSourcesContext
{
    AppMediaSourceContext_t videoContext;
    AppMediaSourceContext_t audioContext;

    uint8_t isPortStarted;
    AppMediaSourceOnMediaSinkHook onMediaSinkHookFunc;
    void * pOnMediaSinkHookCustom;
} AppMediaSourcesContext_t;

int32_t AppMediaSource_Init( AppMediaSourcesContext_t * pCtx,
                             AppMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                             void * pOnMediaSinkHookCustom );
int32_t AppMediaSource_GetVideoTransceiver( AppMediaSourcesContext_t * pCtx,
                                            Transceiver_t ** ppVideoTranceiver );
int32_t AppMediaSource_GetAudioTransceiver( AppMediaSourcesContext_t * pCtx,
                                            Transceiver_t ** ppAudioTranceiver );

#ifdef __cplusplus
}
#endif

#endif /* APP_MEDIA_SOURCE_H */
