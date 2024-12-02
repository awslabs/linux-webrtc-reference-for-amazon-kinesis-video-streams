#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

typedef enum WebsocketResult
{
    WEBSOCKET_RESULT_OK = 0,
    WEBSOCKET_RESULT_FAIL,
    WEBSOCKET_RESULT_BAD_PARAMETER,
} WebsocketResult_t;

typedef struct WebsocketServerInfo
{
    char * pUrl;
    size_t urlLength;
    uint16_t port;
} WebsocketServerInfo_t;

typedef WebsocketResult_t (*WebsocketMessageCallback_t)( char *pMessage, size_t messageLength, void *pUserContext );

WebsocketResult_t Websocket_Init( void * pCredential, WebsocketMessageCallback_t rxCallback, void *pRxCallbackContext );
WebsocketResult_t Websocket_Connect( WebsocketServerInfo_t * pServerInfo );
WebsocketResult_t Websocket_Disconnect( void );
WebsocketResult_t Websocket_Send( char *pMessage, size_t messageLength );
WebsocketResult_t Websocket_Recv();
WebsocketResult_t Websocket_Signal();

#ifdef __cplusplus
}
#endif

#endif /* WEBSOCKET_H */
