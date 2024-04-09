#ifndef NETWORKING_LIBWEBOSCKETS_PRIVATE_H
#define NETWORKING_LIBWEBOSCKETS_PRIVATE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "networkingLibwebsockets.h"

/* Util functions */
NetworkingLibwebsocketsResult_t NetworkingLibwebsockets_Init( NetworkingLibwebsocketsCredentials_t * pCredential );
NetworkingLibwebsocketsResult_t getUrlHost( char *pUrl, size_t urlLength, char **ppStart, size_t *pHostLength );
NetworkingLibwebsocketsResult_t getPathFromUrl( char *pUrl, size_t urlLength, char **ppPath, size_t *pPathLength );
NetworkingLibwebsocketsResult_t getIso8601CurrentTime( char **ppDate, size_t * pDateLength );
NetworkingLibwebsocketsResult_t generateAuthorizationHeader();

/* HTTP functions */
int32_t lwsHttpCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize);

/* Websocket functions */
int32_t lwsWebsocketCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize);

extern NetworkingLibwebsocketContext_t networkingLibwebsocketContext;

#ifdef __cplusplus
}
#endif

#endif /* NETWORKING_LIBWEBOSCKETS_PRIVATE_H */
