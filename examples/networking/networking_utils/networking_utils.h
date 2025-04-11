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

uint64_t NetworkingUtils_GetCurrentTimeSec( void * pTick );
uint64_t NetworkingUtils_GetCurrentTimeUs( void * pTick );
uint64_t NetworkingUtils_GetTimeFromIso8601( const char * pDate,
                                             size_t dateLength );
uint64_t NetworkingUtils_GetNTPTimeFromUnixTimeUs( uint64_t timeUs );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* NETWORKING_UTILS_H */
