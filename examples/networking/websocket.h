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

WebsocketResult_t Websocket_Init( void * pCredential );
WebsocketResult_t Websocket_Connect( WebsocketServerInfo_t * pServerInfo );

#ifdef __cplusplus
}
#endif

#endif /* WEBSOCKET_H */
