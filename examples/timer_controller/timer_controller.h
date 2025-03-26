#ifndef TIMER_CONTROLLER_H
#define TIMER_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

typedef enum TimerControllerResult
{
    TIMER_CONTROLLER_RESULT_OK = 0,
    TIMER_CONTROLLER_RESULT_SET,
    TIMER_CONTROLLER_RESULT_NOT_SET,
    TIMER_CONTROLLER_RESULT_BAD_PARAMETER,
    TIMER_CONTROLLER_RESULT_FAIL_TIMER_CREATE,
    TIMER_CONTROLLER_RESULT_FAIL_TIMER_SET,
    TIMER_CONTROLLER_RESULT_FAIL_GETTIME,
} TimerControllerResult_t;

typedef void (* TimerControllerTimerExpireCallback)( void * pUserContext );

typedef struct TimerHandler
{
    timer_t timerId;
    TimerControllerTimerExpireCallback onTimerExpire;
    void * pUserContext;
} TimerHandler_t;

TimerControllerResult_t TimerController_Create( TimerHandler_t * pTimerHandler,
                                                TimerControllerTimerExpireCallback onTimerExpire,
                                                void * pUserContext );
TimerControllerResult_t TimerController_SetTimer( TimerHandler_t * pTimerHandler,
                                                  uint32_t initialTimeMs,
                                                  uint32_t repeatTimeMs );
void TimerController_Reset( TimerHandler_t * pTimerHandler );
void TimerController_Delete( TimerHandler_t * pTimerHandler );
TimerControllerResult_t TimerController_IsTimerSet( TimerHandler_t * pTimerHandler );

#ifdef __cplusplus
}
#endif

#endif /* TIMER_CONTROLLER_H */
