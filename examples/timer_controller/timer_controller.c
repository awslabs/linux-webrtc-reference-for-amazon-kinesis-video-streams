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

#include "timer_controller.h"
#include "logging.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>

static void generalTimerCallback(union sigval sv) {
  TimerHandler_t *pTimerHandler = (TimerHandler_t *)sv.sival_ptr;

  if (pTimerHandler == NULL) {
    LogWarn(("Unexpected behavior, timer expires with NULL data pointer"));
  } else {
    pTimerHandler->onTimerExpire(pTimerHandler->pUserContext);
  }
}

TimerControllerResult_t
TimerController_Create(TimerHandler_t *pTimerHandler,
                       TimerControllerTimerExpireCallback onTimerExpire,
                       void *pUserContext) {
  TimerControllerResult_t ret = TIMER_CONTROLLER_RESULT_OK;
  struct sigevent sigEvent;

  if ((pTimerHandler == NULL) || (onTimerExpire == NULL)) {
    ret = TIMER_CONTROLLER_RESULT_BAD_PARAMETER;
  }

  if (ret == TIMER_CONTROLLER_RESULT_OK) {
    // Set timer handler
    pTimerHandler->onTimerExpire = onTimerExpire;
    pTimerHandler->pUserContext = pUserContext;

    // Set up the signal handler
    memset(&sigEvent, 0, sizeof(sigEvent));
    sigEvent.sigev_notify = SIGEV_THREAD;
    sigEvent.sigev_notify_function = &generalTimerCallback;
    sigEvent.sigev_value.sival_ptr = pTimerHandler;

    // Create the timer
    if (timer_create(CLOCK_REALTIME, &sigEvent, &pTimerHandler->timerId) != 0) {
      LogError(("Fail to create timer, errno: %s", strerror(errno)));
      ret = TIMER_CONTROLLER_RESULT_FAIL_TIMER_CREATE;
    }
  }

  return ret;
}

TimerControllerResult_t TimerController_SetTimer(TimerHandler_t *pTimerHandler,
                                                 uint32_t initialTimeMs,
                                                 uint32_t repeatTimeMs) {
  TimerControllerResult_t ret = TIMER_CONTROLLER_RESULT_OK;
  struct itimerspec its;

  if (pTimerHandler == NULL) {
    ret = TIMER_CONTROLLER_RESULT_BAD_PARAMETER;
  }

  if (ret == TIMER_CONTROLLER_RESULT_OK) {
    // Set the timer interval
    its.it_value.tv_sec = initialTimeMs / 1000;
    its.it_value.tv_nsec = (initialTimeMs % 1000) * 1000000;
    its.it_interval.tv_sec = repeatTimeMs / 1000;
    its.it_interval.tv_nsec = (repeatTimeMs % 1000) * 1000000;

    // Start the timer
    if (timer_settime(pTimerHandler->timerId, 0, &its, NULL) != 0) {
      LogError(("Fail to set timer, errno: %s", strerror(errno)));
      ret = TIMER_CONTROLLER_RESULT_FAIL_TIMER_SET;
    }
  }

  return ret;
}

void TimerController_Reset(TimerHandler_t *pTimerHandler) {
  if (pTimerHandler != NULL) {
    // Cancel the timer
    if (TimerController_SetTimer(pTimerHandler, 0U, 0U) !=
        TIMER_CONTROLLER_RESULT_OK) {
      LogError(("Fail to reset timer, errno: %s", strerror(errno)));
    }
  }
}

void TimerController_Delete(TimerHandler_t *pTimerHandler) {
  if (pTimerHandler != NULL) {
    // Delete the timer
    if (timer_delete(pTimerHandler->timerId) != 0) {
      LogError(("Fail to delete timer, errno: %s", strerror(errno)));
    }
  }
}

TimerControllerResult_t
TimerController_IsTimerSet(TimerHandler_t *pTimerHandler) {
  TimerControllerResult_t ret = TIMER_CONTROLLER_RESULT_OK;
  struct itimerspec its;

  if (pTimerHandler == NULL) {
    ret = TIMER_CONTROLLER_RESULT_BAD_PARAMETER;
  }

  if (timer_gettime(pTimerHandler->timerId, &its) != 0) {
    LogError(("timer_gettime fail, errno: %s", strerror(errno)));
    ret = TIMER_CONTROLLER_RESULT_FAIL_GETTIME;
  }

  if ((its.it_value.tv_sec == 0) && (its.it_value.tv_nsec == 0)) {
    ret = TIMER_CONTROLLER_RESULT_NOT_SET;
  } else {
    ret = TIMER_CONTROLLER_RESULT_SET;
  }

  return ret;
}
