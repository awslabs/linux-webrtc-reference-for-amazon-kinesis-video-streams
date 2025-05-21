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

#include "ring_buffer.h"

RingBufferResult_t RingBuffer_Init( RingBuffer_t * pRingBuffer )
{
    RingBufferResult_t ret = RING_BUFFER_RESULT_OK;

    if( pRingBuffer == NULL )
    {
        ret = RING_BUFFER_RESULT_BAD_PARAM;
    }

    if( ret == RING_BUFFER_RESULT_OK )
    {
        pRingBuffer->pHead = NULL;
        pRingBuffer->pTail = NULL;
        pthread_mutex_init( &( pRingBuffer->lock ), NULL );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

RingBufferResult_t RingBuffer_Insert( RingBuffer_t * pRingBuffer,
                                      char * pBuffer,
                                      size_t bufferLength )
{
    RingBufferResult_t ret = RING_BUFFER_RESULT_OK;
    RingBufferElementInternal_t * pElement;

    if( ( pRingBuffer == NULL ) ||
        ( pBuffer == NULL ) ||
        ( bufferLength == 0 ) )
    {
        ret = RING_BUFFER_RESULT_BAD_PARAM;
    }

    if( ret == RING_BUFFER_RESULT_OK )
    {
        pElement = ( RingBufferElementInternal_t * ) malloc( sizeof( RingBufferElementInternal_t ) );

        if( pElement == NULL )
        {
            ret = RING_BUFFER_RESULT_OUT_OF_MEM;
        }
    }

    if( ret == RING_BUFFER_RESULT_OK )
    {
        pElement->element.pBuffer = pBuffer;
        pElement->element.bufferLength = bufferLength;
        pElement->element.currentIndex = 0;
        pElement->pNext = NULL;

        pthread_mutex_lock( &( pRingBuffer->lock ) );
        {
            if( pRingBuffer->pHead == NULL )
            {
                pRingBuffer->pHead = pRingBuffer->pTail = pElement;
            }
            else
            {
                pRingBuffer->pTail->pNext = pElement;
                pRingBuffer->pTail = pElement;
            }
        }
        pthread_mutex_unlock( &( pRingBuffer->lock ) );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

RingBufferResult_t RingBuffer_GetHeadEntry( RingBuffer_t * pRingBuffer,
                                            RingBufferElement_t ** ppElement )
{
    RingBufferResult_t ret = RING_BUFFER_RESULT_OK;

    if( ( pRingBuffer == NULL ) ||
        ( ppElement == NULL ) )
    {
        ret = RING_BUFFER_RESULT_BAD_PARAM;
    }

    if( ret == RING_BUFFER_RESULT_OK )
    {
        pthread_mutex_lock( &( pRingBuffer->lock ) );
        {
            if( pRingBuffer->pHead == NULL )
            {
                ret = RING_BUFFER_RESULT_EMPTY;
            }
            else
            {
                *ppElement = &( pRingBuffer->pHead->element );
            }
        }
        pthread_mutex_unlock( &( pRingBuffer->lock ) );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

RingBufferResult_t RingBuffer_RemoveHeadEntry( RingBuffer_t * pRingBuffer,
                                               RingBufferElement_t * pElement )
{
    RingBufferResult_t ret = RING_BUFFER_RESULT_OK;
    RingBufferElementInternal_t * pOldHead = NULL;

    if( ( pRingBuffer == NULL ) ||
        ( pElement == NULL ) )
    {
        ret = RING_BUFFER_RESULT_BAD_PARAM;
    }

    if( ret == RING_BUFFER_RESULT_OK )
    {
        pthread_mutex_lock( &( pRingBuffer->lock ) );
        {
            if( &( pRingBuffer->pHead->element ) != pElement )
            {
                ret = RING_BUFFER_RESULT_INCONSISTENT;
            }
            else
            {
                pOldHead = pRingBuffer->pHead;
                pRingBuffer->pHead = pRingBuffer->pHead->pNext;
            }
        }
        pthread_mutex_unlock( &( pRingBuffer->lock ) );

        free( pOldHead );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/
