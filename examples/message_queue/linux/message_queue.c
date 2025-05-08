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

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include "logging.h"
#include "message_queue.h"

void MessageQueue_Destroy( MessageQueueHandler_t * pMessageQueueHandler,
                           const char * pQueueName )
{
    if( pMessageQueueHandler != NULL )
    {
        if( mq_close( pMessageQueueHandler->messageQueue ) == -1 )
        {
            LogError( ( "mq_close error, errno: %s, queue name: %s", strerror( errno ), pMessageQueueHandler->pQueueName ) );
        }

        if( mq_unlink( pMessageQueueHandler->pQueueName ) == -1 )
        {
            LogError( ( "mq_unlink error, errno: %s, queue name: %s", strerror( errno ), pMessageQueueHandler->pQueueName ) );
        }
    }
    else
    {
        /* Remove the queue with same name. */
        mq_unlink( pQueueName );
    }
}

MessageQueueResult_t MessageQueue_Create( MessageQueueHandler_t * pMessageQueueHandler,
                                          const char * pQueueName,
                                          size_t messageMaxLength,
                                          size_t messageQueueMaxNum )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;
    struct mq_attr attr;

    if( ( pMessageQueueHandler == NULL ) || ( pQueueName == NULL ) )
    {
        ret = MESSAGE_QUEUE_RESULT_BAD_PARAMETER;
    }

    if( ret == MESSAGE_QUEUE_RESULT_OK )
    {
        memset( &attr, 0, sizeof( struct mq_attr ) );
        attr.mq_msgsize = messageMaxLength;
        attr.mq_maxmsg = messageQueueMaxNum;

        pMessageQueueHandler->messageQueue = mq_open( pQueueName, O_RDWR | O_CREAT, 0666, &attr );

        if( pMessageQueueHandler->messageQueue == ( mqd_t ) -1 )
        {
            ret = MESSAGE_QUEUE_RESULT_MQ_OPEN_FAILED;
        }
        else
        {
            pMessageQueueHandler->pQueueName = pQueueName;
        }
    }

    return ret;
}

MessageQueueResult_t MessageQueue_Send( MessageQueueHandler_t * pMessageQueueHandler,
                                        void * pMessage,
                                        size_t messageLength )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;

    if( ( pMessageQueueHandler == NULL ) || ( pMessage == NULL ) )
    {
        ret = MESSAGE_QUEUE_RESULT_BAD_PARAMETER;
    }

    if( ret == MESSAGE_QUEUE_RESULT_OK )
    {
        if( mq_send( pMessageQueueHandler->messageQueue, pMessage, messageLength, 0 ) == -1 )
        {
            LogError( ( "mq_send returns failed" ) );
            ret = MESSAGE_QUEUE_RESULT_MQ_SEND_FAILED;
        }
    }

    return ret;
}

MessageQueueResult_t MessageQueue_Recv( MessageQueueHandler_t * pMessageQueueHandler,
                                        void * pMessage,
                                        size_t * pMessageLength )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;
    int32_t recvLength;
    unsigned int recvPriority = 0;

    if( ( pMessageQueueHandler == NULL ) || ( pMessage == NULL ) )
    {
        ret = MESSAGE_QUEUE_RESULT_BAD_PARAMETER;
    }

    if( ret == MESSAGE_QUEUE_RESULT_OK )
    {
        recvLength = mq_receive( pMessageQueueHandler->messageQueue, pMessage, *pMessageLength, &recvPriority );
        if( recvLength == -1 )
        {
            LogError( ( "mq_receive returns failed" ) );
            ret = MESSAGE_QUEUE_RESULT_MQ_RECV_FAILED;
        }
    }

    if( ret == MESSAGE_QUEUE_RESULT_OK )
    {
        *pMessageLength = recvLength;
    }

    return ret;
}

MessageQueueResult_t MessageQueue_IsEmpty( MessageQueueHandler_t * pMessageQueueHandler )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;
    struct mq_attr attr;

    if( mq_getattr( pMessageQueueHandler->messageQueue, &attr ) == -1 )
    {
        LogError( ( "mq_getattr returns failed" ) );
        ret = MESSAGE_QUEUE_RESULT_MQ_GETATTR_FAILED;
    }

    if( ret == MESSAGE_QUEUE_RESULT_OK )
    {
        if( attr.mq_curmsgs <= 0 )
        {
            ret = MESSAGE_QUEUE_RESULT_MQ_IS_EMPTY;
        }
        else
        {
            ret = MESSAGE_QUEUE_RESULT_MQ_HAVE_MESSAGE;
        }
    }

    return ret;
}

MessageQueueResult_t MessageQueue_IsFull( MessageQueueHandler_t * pMessageQueueHandler )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;
    struct mq_attr attr;

    if( mq_getattr( pMessageQueueHandler->messageQueue, &attr ) == -1 )
    {
        LogError( ( "mq_getattr returns failed" ) );
        ret = MESSAGE_QUEUE_RESULT_MQ_GETATTR_FAILED;
    }

    if( ret == MESSAGE_QUEUE_RESULT_OK )
    {
        if( attr.mq_curmsgs == attr.mq_maxmsg )
        {
            ret = MESSAGE_QUEUE_RESULT_MQ_IS_FULL;
        }
        else
        {
            ret = MESSAGE_QUEUE_RESULT_MQ_IS_NOT_FULL;
        }
    }

    return ret;
}

MessageQueueResult_t MessageQueue_AttachPoll( MessageQueueHandler_t * pMessageQueueHandler,
                                              struct pollfd * pPollFd,
                                              uint32_t PollEvents )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;

    if( ( pMessageQueueHandler == NULL ) || ( pPollFd == NULL ) )
    {
        ret = MESSAGE_QUEUE_RESULT_BAD_PARAMETER;
    }

    if( ret == MESSAGE_QUEUE_RESULT_OK )
    {
        pPollFd->fd = pMessageQueueHandler->messageQueue;
        pPollFd->events = PollEvents;
    }

    return ret;
}
