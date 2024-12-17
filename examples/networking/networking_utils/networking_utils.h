#ifndef NETWORKING_UTILS_H
#define NETWORKING_UTILS_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

uint64_t NetworkingUtils_GetCurrentTimeSec( void * pTick );
uint64_t NetworkingUtils_GetCurrentTimeUs( void * pTick );

#ifdef __cplusplus
}
#endif

#endif /* NETWORKING_UTILS_H */
