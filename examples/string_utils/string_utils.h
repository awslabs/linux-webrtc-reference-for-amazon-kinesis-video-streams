#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

typedef enum StringUtilsResult
{
    STRING_UTILS_RESULT_OK = 0,
    STRING_UTILS_RESULT_BAD_PARAMETER,
    STRING_UTILS_RESULT_NON_NUMBERIC_STRING,
} StringUtilsResult_t;

StringUtilsResult_t StringUtils_ConvertStringToUl( const char *pStr, size_t strLength, uint32_t *pOutUl );

#ifdef __cplusplus
}
#endif

#endif /* STRING_UTILS_H */
