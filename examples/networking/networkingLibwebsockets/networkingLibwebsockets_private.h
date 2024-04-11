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
} NetworkingLibwebsocketHttpVerb_t;

/* Refer to https://docs.aws.amazon.com/IAM/latest/UserGuide/create-signed-request.html
 * to create a struct that needed for generating authorzation header. */
typedef struct NetworkingLibwebsocketCanonicalRequest
{
    NetworkingLibwebsocketHttpVerb_t verb;
    char *pPath; // For canonical URI
    size_t pathLength;
    char *pCanonicalQueryString; // Canonical query string
    size_t canonicalQueryStringLength;
    char *pCanonicalHeaders; // Canonical headers
    size_t canonicalHeadersLength;
    char *pPayload; // Un-hashed payload
    size_t payloadLength;
} NetworkingLibwebsocketCanonicalRequest_t;

/* Util functions */
NetworkingLibwebsocketsResult_t NetworkingLibwebsockets_Init( NetworkingLibwebsocketsCredentials_t * pCredential );
NetworkingLibwebsocketsResult_t getUrlHost( char *pUrl, size_t urlLength, char **ppStart, size_t *pHostLength );
NetworkingLibwebsocketsResult_t getPathFromUrl( char *pUrl, size_t urlLength, char **ppPath, size_t *pPathLength );
NetworkingLibwebsocketsResult_t getIso8601CurrentTime( char **ppDate, size_t * pDateLength );
NetworkingLibwebsocketsResult_t performLwsConnect( char *pHost, size_t hostLength, uint16_t port, uint8_t isHttp );
NetworkingLibwebsocketsResult_t generateAuthorizationHeader( NetworkingLibwebsocketCanonicalRequest_t *pCanonicalRequest );
NetworkingLibwebsocketsResult_t uriEncodedString( char *pSrc, size_t srcLength, char *pDst, size_t *pDstLength );

/* HTTP functions */
int32_t lwsHttpCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize);

/* Websocket functions */
int32_t lwsWebsocketCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize);

extern NetworkingLibwebsocketContext_t networkingLibwebsocketContext;

#ifdef __cplusplus
}
#endif

#endif /* NETWORKING_LIBWEBOSCKETS_PRIVATE_H */
