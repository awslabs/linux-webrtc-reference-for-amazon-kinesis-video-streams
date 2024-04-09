#ifndef HTTP_H
#define HTTP_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

typedef enum HttpResult
{
    HTTP_RESULT_OK = 0,
    HTTP_RESULT_FAIL,
    HTTP_RESULT_BAD_PARAMETER,
} HttpResult_t;

typedef struct HttpRequest
{
    char * pUrl;
    size_t urlLength;
    char * pBody;
    size_t bodyLength;
} HttpRequest_t;

typedef struct HttpResponse
{
    char * pBuffer;
    size_t bufferLength;
} HttpResponse_t;

HttpResult_t Http_Init( void * pCredential );
HttpResult_t Http_Send( HttpRequest_t * pRequest, size_t timeoutMs, HttpResponse_t *pResponse );

#ifdef __cplusplus
}
#endif

#endif /* HTTP_H */
