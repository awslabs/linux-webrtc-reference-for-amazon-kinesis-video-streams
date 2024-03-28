#include <stdio.h>
#include "libwebsockets.h"

int32_t lwsCallback(struct lws* wsi, enum lws_callback_reasons reason, void * pUser, void * pDataIn, size_t dataSize)
{
    return 0;
}

int main()
{
    struct lws_context_creation_info creationInfo;
    const lws_retry_bo_t retryPolicy = {
        .secs_since_valid_ping = 10,
        .secs_since_valid_hangup = 7200,
    };
    struct lws_context * pLwsContext;
    struct lws_protocols protocols[3];

    protocols[0].name = "https";
    protocols[0].callback = lwsCallback;
    protocols[1].name = "wss";
    protocols[1].callback = lwsCallback;

    memset(&creationInfo, 0x00, sizeof(struct lws_context_creation_info));
    creationInfo.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    creationInfo.port = CONTEXT_PORT_NO_LISTEN;
    creationInfo.protocols = protocols;
    creationInfo.timeout_secs = 10;
    creationInfo.gid = -1;
    creationInfo.uid = -1;
    creationInfo.client_ssl_ca_filepath = "";
    creationInfo.client_ssl_cipher_list = "HIGH:!PSK:!RSP:!eNULL:!aNULL:!RC4:!MD5:!DES:!3DES:!aDH:!kDH:!DSS";
    creationInfo.ka_time = 1;
    creationInfo.ka_probes = 1;
    creationInfo.ka_interval = 1;
    creationInfo.retry_and_idle_policy = &retryPolicy;

    pLwsContext = lws_create_context(&creationInfo);
    (void) pLwsContext;

    return 0;
}
