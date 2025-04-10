#ifndef METRIC_H
#define METRIC_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include <inttypes.h>

/* FreeRTOS includes. */
// #include "FreeRTOS.h"
// #include "semphr.h"
#include <pthread.h>

typedef enum MetricEvent
{
    METRIC_EVENT_NONE = 0,

    /* Signaling Events */
    METRIC_EVENT_SIGNALING_DESCRIBE_CHANNEL,
    METRIC_EVENT_SIGNALING_GET_ENDPOINTS,
    METRIC_EVENT_SIGNALING_GET_ICE_SERVER_LIST,
    METRIC_EVENT_SIGNALING_CONNECT_WSS_SERVER,
    METRIC_EVENT_SIGNALING_GET_CREDENTIALS,
    METRIC_EVENT_SIGNALING_JOIN_STORAGE_SESSION,

    /* ICE Events. */
    METRIC_EVENT_ICE_GATHER_HOST_CANDIDATES,
    METRIC_EVENT_ICE_GATHER_SRFLX_CANDIDATES,
    METRIC_EVENT_ICE_FIND_P2P_CONNECTION,

    /* Peer Connection Events. */
    METRIC_EVENT_PC_DTLS_HANDSHAKING,

    /* Combine case. */
    METRIC_EVENT_SENDING_FIRST_FRAME,

    METRIC_EVENT_MAX,
} MetricEvent_t;

typedef enum MetricEventState
{
    METRIC_EVENT_STATE_NONE = 0,
    METRIC_EVENT_STATE_RECORDING,
    METRIC_EVENT_STATE_RECORDED,
} MetricEventState_t;

typedef struct MetricEventRecord
{
    MetricEventState_t state;
    uint64_t startTimeUs;
    uint64_t endTimeUs;
} MetricEventRecord_t;

typedef struct MetricContext
{
    uint8_t isInit;
    MetricEventRecord_t eventRecords[ METRIC_EVENT_MAX ];
    pthread_mutex_t mutex;
} MetricContext_t;

void Metric_Init( void );
void Metric_StartEvent( MetricEvent_t event );
void Metric_EndEvent( MetricEvent_t event );
void Metric_PrintMetrics( void );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* METRIC_H */
