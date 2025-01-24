#ifndef NETWORKING_LIBWEBOSCKETS_H
#define NETWORKING_LIBWEBOSCKETS_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <openssl/evp.h>
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
#define NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH ( 4096 )
#define NETWORKING_LWS_SIGV4_AUTH_BUFFER_LENGTH ( 2048 )
#define NETWORKING_LWS_URI_HOST_MAX_LENGTH ( 128 )
#define NETWORKING_LWS_QUERY_PARAM_MAX_NUM ( 4 )
#define NETWORKING_LWS_WEBSOCKET_RX_BUFFER_LENGTH ( 10000 )
#define NETWORKING_LWS_PROTOCOLS_NUM ( 2 )
#define NETWORKING_LWS_PROTOCOLS_HTTP_INDEX ( 0 )
#define NETWORKING_LWS_PROTOCOLS_WEBSOCKET_INDEX ( 1 )
#define NETWORKING_LWS_RING_BUFFER_NUM ( 5 )
#define NETWORKING_LWS_RING_BUFFER_LENGTH ( LWS_PRE + 10000 )

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
    NETWORKING_LIBWEBSOCKETS_RESULT_HOST_BUFFER_TOO_SMALL,
    NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL,
    NETWORKING_LIBWEBSOCKETS_RESULT_INVALID_AUTH_VERB,
    NETWORKING_LIBWEBSOCKETS_RESULT_INVALID_HTTP_VERB,
    NETWORKING_LIBWEBSOCKETS_RESULT_UNEXPECTED_WEBSOCKET_URL,
    NETWORKING_LIBWEBSOCKETS_RESULT_URI_ENCODED_BUFFER_TOO_SMALL,
    NETWORKING_LIBWEBSOCKETS_RESULT_UNKNOWN_MESSAGE,
    NETWORKING_LIBWEBSOCKETS_RESULT_BASE64_DECODE_FAIL,
    NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_EMPTY,
    NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_FULL,
    NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_TOO_SMALL,
    NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_FREE_WRONG_INDEX,
    NETWORKING_LIBWEBSOCKETS_RESULT_FAIL_CONNECT,
} NetworkingLibwebsocketsResult_t;

typedef struct NetworkingLibwebsocketsAppendHeaders
{
    char * pChannelArn;
    size_t channelArnLength;
    char * pUserAgent;
    size_t userAgentLength;
    char * pHost;
    size_t hostLength;
    char * pDate;
    size_t dateLength;
    char * pContentType;
    size_t contentTypeLength;
    size_t contentLength;
    char * pIotThingName;
    size_t iotThingNameLength;
    char * pAuthorization;
    size_t authorizationLength;
    char * pSignature;
    size_t signatureLength;
} NetworkingLibwebsocketsAppendHeaders_t;

typedef struct NetworkingLibwebsocketsCredentials
{
    /* user-agent */
    char * pUserAgent;
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

    /* IoT thing credentials for role alias. */
    char * pIotThingCertPath;
    size_t iotThingCertPathLength;
    char * pIotThingPrivateKeyPath;
    size_t iotThingPrivateKeyPathLength;
    char * pIotThingName;
    size_t iotThingNameLength;
    char * pSessionToken;
    size_t sessionTokenLength;

    uint64_t expirationSeconds;
} NetworkingLibwebsocketsCredentials_t;

typedef struct NetworkingLibwebsocketBufferInfo
{
    uint8_t buffer[ NETWORKING_LWS_RING_BUFFER_LENGTH ];
    size_t bufferLength;
    size_t offset;
    WebsocketMessageCallback_t txCallback;
    void * pTxCallbackContext;
} NetworkingLibwebsocketBufferInfo_t;

typedef struct NetworkingLibwebsocketRingBuffer
{
    size_t start;
    size_t end;
    NetworkingLibwebsocketBufferInfo_t bufferInfo[ NETWORKING_LWS_RING_BUFFER_NUM ];
} NetworkingLibwebsocketRingBuffer_t;

typedef struct NetworkingLibwebsocketContext
{
    struct lws_context * pLwsContext;
    struct lws * pLws[ NETWORKING_LWS_PROTOCOLS_NUM ];
    uint8_t terminateLwsService;
    char pLwsBuffer[ NETWORKING_LWS_MAX_BUFFER_LENGTH ];

    /* append HTTP headers */
    NetworkingLibwebsocketsAppendHeaders_t appendHeaders;
    HttpRequest_t * pRequest;
    HttpResponse_t * pResponse;

    NetworkingLibwebsocketsCredentials_t libwebsocketsCredentials;

    /* SigV4 credential */
    char pathBuffer[ NETWORKING_LWS_MAX_URI_LENGTH + 1 ];
    uint32_t pathBufferWrittenLength;
    char sigv4Metadatabuffer[ NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH ];
    char sigv4AuthBuffer[ NETWORKING_LWS_SIGV4_AUTH_BUFFER_LENGTH ];
    size_t sigv4AuthLen;
    SigV4Credentials_t sigv4Credential;

    /* OpenSSL EVP context. */
    EVP_MD_CTX * pEvpMdCtx;

    /* Rx path: callback user to handle received message. */
    WebsocketMessageCallback_t websocketRxCallback;
    void * pWebsocketRxCallbackContext;
    char websocketRxBuffer[ NETWORKING_LWS_WEBSOCKET_RX_BUFFER_LENGTH ];
    size_t websocketRxBufferLength;

    /* Tx path: notify user that Tx sent done. */
    NetworkingLibwebsocketRingBuffer_t websocketTxRingBuffer;
} NetworkingLibwebsocketContext_t;

#ifdef __cplusplus
}
#endif

#endif /* NETWORKING_LIBWEBOSCKETS_H */
