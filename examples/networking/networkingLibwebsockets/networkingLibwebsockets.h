#ifndef NETWORKING_LIBWEBOSCKETS_H
#define NETWORKING_LIBWEBOSCKETS_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "libwebsockets.h"
#include "sigv4.h"

#include "http.h"
#include "websocket.h"

#define NETWORKING_LWS_DEFAULT_REGION "us-west-2"
#define NETWORKING_LWS_KVS_SERVICE_NAME "kinesisvideo"
#define NETWORKING_LWS_TIME_LENGTH ( 17 ) /* length of ISO8601 format (e.g. 20111008T070709Z) with NULL terminator */
#define NETWORKING_LWS_MAX_URI_LENGTH ( 10000 )
#define NETWORKING_LWS_MAX_BUFFER_LENGTH ( LWS_PRE + 2048 ) // General buffer for libwebsockets to send/read data.
#define NETWORKING_LWS_USER_AGENT_NAME_MAX_LENGTH ( 128 )
#define NETWORKING_SIGV4_AUTH_BUFFER_LENGTH ( 2048 )

typedef enum NetworkingLibwebsocketsResult
{
    NETWORKING_LIBWEBSOCKETS_RESULT_OK = 0,
    NETWORKING_LIBWEBSOCKETS_RESULT_FAIL,
    NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER,
    NETWORKING_LIBWEBSOCKETS_RESULT_BASE = 0x1000,
    NETWORKING_LIBWEBSOCKETS_RESULT_SCHEMA_DELIMITER_NOT_FOUND,
    NETWORKING_LIBWEBSOCKETS_RESULT_EXCEED_URL_LENGTH,
    NETWORKING_LIBWEBSOCKETS_RESULT_TIME_BUFFER_TOO_SMALL,
    NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL,
    NETWORKING_LIBWEBSOCKETS_RESULT_AUTH_BUFFER_TOO_SMALL,
    NETWORKING_LIBWEBSOCKETS_RESULT_SIGV4_GENERATE_AUTH_FAIL,
    NETWORKING_LIBWEBSOCKETS_RESULT_USER_AGENT_NAME_TOO_LONG,
    NETWORKING_LIBWEBSOCKETS_RESULT_INIT_LWS_CONTEXT_FAIL,
    NETWORKING_LIBWEBSOCKETS_RESULT_PATH_BUFFER_TOO_SMALL,
} NetworkingLibwebsocketsResult_t;

typedef struct NetworkingLibwebsocketsAppendHeaders
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
} NetworkingLibwebsocketsAppendHeaders_t;

typedef struct NetworkingLibwebsocketsCredentials
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
} NetworkingLibwebsocketsCredentials_t;

typedef struct NetworkingLibwebsocketContext
{
    struct lws_context *pLwsContext;
    struct lws *pLws;
    uint8_t terminateLwsService;
    char pLwsBuffer[ NETWORKING_LWS_MAX_BUFFER_LENGTH ];

    /* append HTTP headers */
    NetworkingLibwebsocketsAppendHeaders_t appendHeaders;
    HttpRequest_t *pRequest;
    HttpResponse_t *pResponse;

    NetworkingLibwebsocketsCredentials_t libwebsocketsCredentials;

    /* SigV4 credential */
    char pathBuffer[ NETWORKING_LWS_MAX_URI_LENGTH + 1 ];
    uint32_t pathBufferWrittenLength;
    char sigv4AuthBuffer[ NETWORKING_SIGV4_AUTH_BUFFER_LENGTH ];
    size_t sigv4AuthLen;
    SigV4Credentials_t sigv4Credential;

    /* OpenSSL SHA256 context. */
    SHA256_CTX sha256Ctx;
} NetworkingLibwebsocketContext_t;

struct HttpContext
{
    NetworkingLibwebsocketContext_t networkingLibwebsocketContext;
};

struct WebsocketContext
{
    NetworkingLibwebsocketContext_t networkingLibwebsocketContext;
};

#ifdef __cplusplus
}
#endif

#endif /* NETWORKING_LIBWEBOSCKETS_H */
