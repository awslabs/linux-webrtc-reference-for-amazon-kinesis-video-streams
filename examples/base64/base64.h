#ifndef BASE64_H
#define BASE64_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

typedef enum Base64Result
{
    BASE64_RESULT_OK = 0,
    BASE64_RESULT_BAD_PARAMETER,
    BASE64_RESULT_INVALID_INPUT,
    BASE64_RESULT_BUFFER_TOO_SMALL,
} Base64Result_t;

Base64Result_t base64Decode( const char *pInputData, size_t inputDataLength, char *pOutputData, size_t *pOutputDataLength );

#ifdef __cplusplus
}
#endif

#endif /* BASE64_H */