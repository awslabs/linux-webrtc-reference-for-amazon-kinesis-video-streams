#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include <stdint.h>

typedef enum StringUtilsResult
{
    STRING_UTILS_RESULT_OK = 0,
    STRING_UTILS_RESULT_BAD_PARAMETER,
    STRING_UTILS_RESULT_NON_NUMBERIC_STRING,
} StringUtilsResult_t;

StringUtilsResult_t StringUtils_ConvertStringToUl( const char * pStr,
                                                   size_t strLength,
                                                   uint32_t * pOutUl );
StringUtilsResult_t StringUtils_ConvertStringToHex( const char * pStr,
                                                    size_t strLength,
                                                    uint32_t * pOutUl );
const char * StringUtils_StrStr( const char * pStr,
                                 size_t strLength,
                                 const char * pPattern,
                                 size_t patternLength );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* STRING_UTILS_H */
