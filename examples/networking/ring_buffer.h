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

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
/*----------------------------------------------------------------------------*/

typedef enum RingBufferResult
{
    RING_BUFFER_RESULT_OK,
    RING_BUFFER_RESULT_BAD_PARAM,
    RING_BUFFER_RESULT_OUT_OF_MEM,
    RING_BUFFER_RESULT_EMPTY,
    RING_BUFFER_RESULT_INCONSISTENT,
} RingBufferResult_t;

/*----------------------------------------------------------------------------*/

typedef struct RingBufferElement
{
    char * pBuffer;
    size_t bufferLength;
    size_t currentIndex;
} RingBufferElement_t;

typedef struct RingBufferElementInternal
{
    RingBufferElement_t element;
    struct RingBufferElementInternal * pNext;
} RingBufferElementInternal_t;

typedef struct RingBuffer
{
    pthread_mutex_t lock;
    RingBufferElementInternal_t * pHead;
    RingBufferElementInternal_t * pTail;
} RingBuffer_t;

/*----------------------------------------------------------------------------*/

RingBufferResult_t RingBuffer_Init( RingBuffer_t * pRingBuffer );

RingBufferResult_t RingBuffer_Insert(
    RingBuffer_t * pRingBuffer, char * pBuffer, size_t bufferLength );

RingBufferResult_t RingBuffer_GetHeadEntry(
    RingBuffer_t * pRingBuffer, RingBufferElement_t ** ppElement );

RingBufferResult_t RingBuffer_RemoveHeadEntry(
    RingBuffer_t * pRingBuffer, RingBufferElement_t * pElement );

/*----------------------------------------------------------------------------*/
