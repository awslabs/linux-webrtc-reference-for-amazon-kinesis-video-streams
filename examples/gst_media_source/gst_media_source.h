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

#ifndef GST_MEDIA_SOURCE_H
#define GST_MEDIA_SOURCE_H

#include "message_queue.h"
#include "peer_connection.h"
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <stdio.h>

typedef struct {
  uint8_t *pData;
  uint32_t size;
  uint64_t timestampUs;
  TransceiverTrackKind_t trackKind;
  uint8_t flags;
  uint8_t freeData; /* indicate user need to free pData after using it */
} WebrtcFrame_t;

typedef struct GstMediaSourcesContext GstMediaSourcesContext_t;
typedef int32_t (*GstMediaSourceOnMediaSinkHook)(void *pCustom,
                                                 WebrtcFrame_t *pFrame);

typedef struct GstMediaSourceContext {
  uint32_t numReadyPeer;
  TransceiverTrackKind_t trackKind;
  GstMediaSourcesContext_t *pSourcesContext;

  /* GStreamer pipeline elements */
  GstElement *pPipeline;
  GstElement *pAppsink;
  GstElement *pEncoder;
  GMainLoop *pMainLoop;
} GstMediaSourceContext_t;

typedef struct GstMediaSourcesContext {
  GstMediaSourceContext_t videoContext;
  GstMediaSourceContext_t audioContext;
  pthread_mutex_t mediaMutex;
  GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc;
  void *pOnMediaSinkHookCustom;
} GstMediaSourcesContext_t;

/**
 * @brief Initialize media source context
 */
int32_t GstMediaSource_Init(GstMediaSourcesContext_t *pCtx,
                            GstMediaSourceOnMediaSinkHook onMediaSinkHookFunc,
                            void *pOnMediaSinkHookCustom);

/**
 * @brief Initialize video transceiver
 */
int32_t GstMediaSource_InitVideoTransceiver(GstMediaSourcesContext_t *pCtx,
                                            Transceiver_t *pVideoTranceiver);

/**
 * @brief Initialize audio transceiver
 */
int32_t GstMediaSource_InitAudioTransceiver(GstMediaSourcesContext_t *pCtx,
                                            Transceiver_t *pAudioTranceiver);

/**
 * @brief Cleanup media source context
 */
int32_t GstMediaSource_Cleanup(GstMediaSourcesContext_t *pCtx);

#endif /* GST_MEDIA_SOURCE_H */
