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

#ifndef APP_MEDIA_SOURCE_H
#define APP_MEDIA_SOURCE_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include "message_queue.h"
#include "peer_connection.h"
#include <stdio.h>

typedef struct {
  uint8_t *pData;
  uint32_t size;
  uint64_t timestampUs;
  TransceiverTrackKind_t trackKind;
  uint8_t freeData; /* indicate user need to free pData after using it */
} WebrtcFrame_t;

typedef struct AppMediaSourcesContext AppMediaSourcesContext_t;
typedef int32_t (*AppMediaSourceOnMediaSinkHook)(void *pCustom,
                                                 WebrtcFrame_t *pFrame);

typedef struct AppMediaSourceContext {
  MessageQueueHandler_t dataQueue;
  uint8_t numReadyPeer;
  TransceiverTrackKind_t trackKind;
  int32_t fileIndex;

  AppMediaSourcesContext_t *pSourcesContext;
} AppMediaSourceContext_t;

typedef struct AppMediaSourcesContext {
  /* Mutex to protect numReadyPeer because we might receive multiple ready/close
   * message from different tasks. */
  pthread_mutex_t mediaMutex;

  AppMediaSourceContext_t videoContext;
  AppMediaSourceContext_t audioContext;

  AppMediaSourceOnMediaSinkHook onMediaSinkHookFunc;
  void *pOnMediaSinkHookCustom;
} AppMediaSourcesContext_t;

int32_t AppMediaSource_Init(AppMediaSourcesContext_t *pCtx,
                            AppMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                            void *pOnMediaSinkHookCustom);
int32_t AppMediaSource_InitVideoTransceiver(AppMediaSourcesContext_t *pCtx,
                                            Transceiver_t *pVideoTranceiver);
int32_t AppMediaSource_InitAudioTransceiver(AppMediaSourcesContext_t *pCtx,
                                            Transceiver_t *pAudioTranceiver);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* APP_MEDIA_SOURCE_H */
