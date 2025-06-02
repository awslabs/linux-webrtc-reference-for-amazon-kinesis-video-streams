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

#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <mqueue.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>

typedef enum MessageQueueResult
{
    MESSAGE_QUEUE_RESULT_OK = 0,
    MESSAGE_QUEUE_RESULT_MQ_IS_EMPTY,
    MESSAGE_QUEUE_RESULT_MQ_HAVE_MESSAGE,
    MESSAGE_QUEUE_RESULT_BAD_PARAMETER,
    MESSAGE_QUEUE_RESULT_MQ_IS_FULL,
    MESSAGE_QUEUE_RESULT_MQ_IS_NOT_FULL,
    MESSAGE_QUEUE_RESULT_MQ_OPEN_FAILED,
    MESSAGE_QUEUE_RESULT_MQ_SEND_FAILED,
    MESSAGE_QUEUE_RESULT_MQ_RECV_FAILED,
    MESSAGE_QUEUE_RESULT_MQ_GETATTR_FAILED,
} MessageQueueResult_t;

typedef struct MessageQueueHandler
{
    const char * pQueueName;
    mqd_t messageQueue;
} MessageQueueHandler_t;

MessageQueueResult_t MessageQueue_Create(
    MessageQueueHandler_t * pMessageQueueHandler, const char * pQueueName,
    size_t messageMaxLength, size_t messageQueueMaxNum );
void MessageQueue_Destroy(
    MessageQueueHandler_t * pMessageQueueHandler, const char * pQueueName );
MessageQueueResult_t MessageQueue_Send(
    MessageQueueHandler_t * pMessageQueueHandler, void * pMessage,
    size_t messageLength );
MessageQueueResult_t MessageQueue_Recv(
    MessageQueueHandler_t * pMessageQueueHandler, void * pMessage,
    size_t * pMessageLength );
MessageQueueResult_t MessageQueue_IsEmpty(
    MessageQueueHandler_t * pMessageQueueHandler );
MessageQueueResult_t MessageQueue_IsFull(
    MessageQueueHandler_t * pMessageQueueHandler );
MessageQueueResult_t MessageQueue_AttachPoll(
    MessageQueueHandler_t * pMessageQueueHandler, struct pollfd * pPollFd,
    uint32_t PollEvents );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* MESSAGE_QUEUE_H */
