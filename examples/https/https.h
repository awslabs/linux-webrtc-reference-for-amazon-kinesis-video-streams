#ifndef HTTPS_H
#define HTTPS_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

typedef enum HttpsResult
{
    HTTPS_RESULT_OK = 0,
    HTTPS_RESULT_FAIL,
    HTTPS_RESULT_BAD_PARAMETER,
} HttpsResult_t;

typedef struct HttpsRequest
{
    char * pUrl;
    size_t urlLength;
    char * pBody;
    size_t bodyLength;
} HttpsRequest_t;

typedef struct HttpsResponse
{
    char * pBuffer;
    size_t bufferLength;
} HttpsResponse_t;

typedef struct HttpsContext HttpsContext_t;

HttpsResult_t Https_Init( HttpsContext_t * pCtx, void * pCredential );
HttpsResult_t Https_Send( HttpsContext_t * pCtx, HttpsRequest_t * pRequest, size_t timeoutMs, HttpsResponse_t *pResponse );

#ifdef __cplusplus
}
#endif

#endif /* HTTPS_H */
