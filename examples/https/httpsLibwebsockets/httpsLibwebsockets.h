#ifndef HTTPS_LIBWEBOSCKETS_H
#define HTTPS_LIBWEBOSCKETS_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "https.h"
#include "libwebsockets.h"
#include "sigv4.h"

#define HTTPS_LWS_USER_AGENT_NAME_MAX_LENGTH ( 128 )

typedef enum HttpsLibwebsocketsResult
{
    HTTPS_LIBWEBSOCKETS_RESULT_OK = 0,
    HTTPS_LIBWEBSOCKETS_RESULT_FAIL,
    HTTPS_LIBWEBSOCKETS_RESULT_BAD_PARAMETER,
    HTTPS_LIBWEBSOCKETS_RESULT_BASE = 0x1000,
    HTTPS_LIBWEBSOCKETS_RESULT_SCHEMA_DELIMITER_NOT_FOUND,
    HTTPS_LIBWEBSOCKETS_RESULT_EXCEED_URL_LENGTH,
    HTTPS_LIBWEBSOCKETS_RESULT_TIME_BUFFER_TOO_SMALL,
    HTTPS_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL,
    HTTPS_LIBWEBSOCKETS_RESULT_AUTH_BUFFER_TOO_SMALL,
    HTTPS_LIBWEBSOCKETS_RESULT_SIGV4_GENERATE_AUTH_FAIL,
    HTTPS_LIBWEBSOCKETS_RESULT_USER_AGENT_NAME_TOO_LONG,
    HTTPS_LIBWEBSOCKETS_RESULT_INIT_LWS_CONTEXT_FAIL,
} HttpsLibwebsocketsResult_t;

typedef struct HttpsLibwebsocketsAppendHeaders
{
    char *pUserAgent;
    size_t userAgentLength;
    char *pHost;
    size_t hostLength;
    char *pDate;
    size_t dateLength;
    char *pContentType;
    size_t contentTypeLength;
    size_t contentLength;
    char *pAuthorization;
    size_t authorizationLength;
} HttpsLibwebsocketsAppendHeaders_t;

typedef struct HttpsLibwebsocketsCredentials
{
    /* user-agent */
    char *pUserAgent;
    size_t userAgentLength;

    /* Region */
    char * pRegion;
    size_t regionLength;

    /* AKSK */
    char * pAccessKeyId;
    size_t accessKeyIdLength;
    char * pSecretAccessKey;
    size_t secretAccessKeyLength;

    /* CA Cert Path */
    char * pCaCertPath;
} HttpsLibwebsocketsCredentials_t;

typedef struct HttpsContext
{
    struct lws_context *pLwsContext;
    struct lws *pLws;
    uint8_t terminateLwsService;

    /* append HTTPS headers */
    HttpsLibwebsocketsAppendHeaders_t appendHeaders;
    HttpsRequest_t *pRequest;
    HttpsResponse_t *pResponse;

    HttpsLibwebsocketsCredentials_t libwebsocketsCredentials;

    /* SigV4 credential */
    SigV4Credentials_t sigv4Credential;

    /* OpenSSL SHA256 context. */
    SHA256_CTX sha256Ctx;
} HttpsContext_t;

#ifdef __cplusplus
}
#endif

#endif /* HTTPS_LIBWEBOSCKETS_H */
