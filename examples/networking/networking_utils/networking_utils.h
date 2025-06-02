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

#ifndef NETWORKING_UTILS_H
#define NETWORKING_UTILS_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include <stdint.h>

/*-----------------------------------------------------------*/

uint64_t NetworkingUtils_GetCurrentTimeSec(void* pTick);
uint64_t NetworkingUtils_GetCurrentTimeUs(void* pTick);
uint64_t NetworkingUtils_GetTimeFromIso8601(const char* pDate,
                                            size_t dateLength);
uint64_t NetworkingUtils_GetNTPTimeFromUnixTimeUs(uint64_t timeUs);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* NETWORKING_UTILS_H */
