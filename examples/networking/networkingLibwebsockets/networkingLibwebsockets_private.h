#ifndef NETWORKING_LIBWEBOSCKETS_PRIVATE_H
#define NETWORKING_LIBWEBOSCKETS_PRIVATE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "networkingLibwebsockets.h"

#define NETWORKING_LWS_STRING_CONTENT_TYPE "content-type"
#define NETWORKING_LWS_STRING_CONTENT_TYPE_VALUE "application/json"

typedef enum NetworkingLibwebsocketHttpVerb
{
    NETWORKING_LWS_HTTP_VERB_NONE = 0,
    NETWORKING_LWS_HTTP_VERB_GET,
    NETWORKING_LWS_HTTP_VERB_POST,
    NETWORKING_LWS_HTTP_VERB_WSS,
} NetworkingLibwebsocketHttpVerb_t;

/* Refer to https://docs.aws.amazon.com/IAM/latest/UserGuide/create-signed-request.html
 * to create a struct that needed for generating authorzation header. */
typedef struct NetworkingLibwebsocketCanonicalRequest
{
    NetworkingLibwebsocketHttpVerb_t verb;
    char * pPath; // For canonical URI
    size_t pathLength;
    char * pCanonicalQueryString; // Canonical query string
    size_t canonicalQueryStringLength;
    char * pCanonicalHeaders; // Canonical headers
    size_t canonicalHeadersLength;
    char * pPayload; // Un-hashed payload
    size_t payloadLength;
} NetworkingLibwebsocketCanonicalRequest_t;

/* Util functions */
NetworkingLibwebsocketsResult_t NetworkingLibwebsockets_Init( NetworkingLibwebsocketsCredentials_t * pCredential );
NetworkingLibwebsocketsResult_t GetUrlHost( char * pUrl,
                                            size_t urlLength,
                                            char ** ppStart,
                                            size_t * pHostLength );
NetworkingLibwebsocketsResult_t GetPathFromUrl( char * pUrl,
                                                size_t urlLength,
                                                char ** ppPath,
                                                size_t * pPathLength );
NetworkingLibwebsocketsResult_t GetIso8601CurrentTime( char ** ppDate,
                                                       size_t * pDateLength );
NetworkingLibwebsocketsResult_t PerformLwsConnect( char * pHost,
                                                   size_t hostLength,
                                                   uint16_t port,
                                                   NetworkingLibwebsocketHttpVerb_t httpVerb );
NetworkingLibwebsocketsResult_t GenerateAuthorizationHeader( NetworkingLibwebsocketCanonicalRequest_t * pCanonicalRequest );
NetworkingLibwebsocketsResult_t UriEncode( char * pSrc,
                                           size_t srcLength,
                                           char * pDst,
                                           size_t * pDstLength );
void NetworkingLibwebsockets_Signal( struct lws_context * pLwsContext );
NetworkingLibwebsocketsResult_t UpdateCredential( NetworkingLibwebsocketsCredentials_t * pCredential );

/* HTTP functions */
int32_t LwsHttpCallbackRoutine( struct lws * wsi,
                                enum lws_callback_reasons reason,
                                void * pUser,
                                void * pDataIn,
                                size_t dataSize );

/* Websocket functions */
int32_t LwsWebsocketCallbackRoutine( struct lws * wsi,
                                     enum lws_callback_reasons reason,
                                     void * pUser,
                                     void * pDataIn,
                                     size_t dataSize );
NetworkingLibwebsocketsResult_t PerformLwsRecv();
NetworkingLibwebsocketsResult_t AllocateRingBuffer( NetworkingLibwebsocketRingBuffer_t * pRingBuffer,
                                                    size_t * pNextIdx );
NetworkingLibwebsocketsResult_t FreeRingBuffer( NetworkingLibwebsocketRingBuffer_t * pRingBuffer,
                                                size_t idx );
NetworkingLibwebsocketsResult_t GetRingBufferCurrentIndex( NetworkingLibwebsocketRingBuffer_t * pRingBuffer,
                                                           size_t * pCurrentIdx );

extern NetworkingLibwebsocketContext_t networkingLibwebsocketContext;

#ifdef __cplusplus
}
#endif

#endif /* NETWORKING_LIBWEBOSCKETS_PRIVATE_H */
