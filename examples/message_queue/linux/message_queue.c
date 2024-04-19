#include <fcntl.h>
#include "logging.h"
#include "message_queue.h"

MessageQueueResult_t MessageQueue_Create( MessageQueueHandler_t *pMessageQueueHandler, const char *pQueueName )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;

    if( pMessageQueueHandler == NULL || pQueueName == NULL )
    {
        ret = MESSAGE_QUEUE_RESULT_BAD_PARAMETER;
    }

    if( ret == MESSAGE_QUEUE_RESULT_OK )
    {
        pMessageQueueHandler->messageQueue = mq_open( pQueueName, O_RDWR | O_CREAT );

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

MessageQueueResult_t MessageQueue_Send( MessageQueueHandler_t *pMessageQueueHandler, void *pMessage, size_t messageLength )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;

    if( pMessageQueueHandler == NULL || pMessage == NULL )
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

MessageQueueResult_t MessageQueue_Recv( MessageQueueHandler_t *pMessageQueueHandler, void *pMessage, size_t *pMessageLength )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;
    int32_t recvLength;
    unsigned int recvPriority = 0;

    if( pMessageQueueHandler == NULL || pMessage == NULL )
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

MessageQueueResult_t MessageQueue_IsEmpty( MessageQueueHandler_t *pMessageQueueHandler )
{
    MessageQueueResult_t ret = MESSAGE_QUEUE_RESULT_OK;
    struct mq_attr attr;

    if( mq_getattr( pMessageQueueHandler->messageQueue, &attr) == -1 )
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
