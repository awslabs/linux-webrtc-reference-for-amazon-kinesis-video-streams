#include "networkingLibwebsockets.h"
#include "networkingLibwebsockets_private.h"

int32_t lwsWebsocketCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize)
{
    int32_t retValue = 0;

    return retValue;
}

WebsocketResult_t Websocket_Connect( WebsocketServerInfo_t * pServerInfo )
{
    WebsocketResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;

    return ret;
}

WebsocketResult_t Websocket_Init( void * pCredential )
{
    NetworkingLibwebsocketsCredentials_t *pNetworkingLibwebsocketsCredentials = (NetworkingLibwebsocketsCredentials_t *)pCredential;

    return NetworkingLibwebsockets_Init( pNetworkingLibwebsocketsCredentials );
}
