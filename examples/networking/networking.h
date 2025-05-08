/* Standard includes. */
#include <stdlib.h>

/* LWS includes. */
#include "libwebsockets.h"

/* Ring buffer includes. */
#include "ring_buffer.h"

/* Logging includes. */
#include "logging.h"

/* Config parameters. TODO aggarg - need to move to a central config file. */
#define HTTP_URI_HOST_BUFFER_LENGTH                 128
#define WEBSOCKET_URI_HOST_BUFFER_LENGTH            128
#define HTTP_URI_PATH_BUFFER_LENGTH                 256
#define WEBSOCKET_URI_PATH_BUFFER_LENGTH            2048
#define SIGV4_METADATA_BUFFER_LENGTH                4096
#define SIGV4_AUTHORIZATION_HEADER_BUFFER_LENGTH    2048
#define HTTP_RX_BUFFER_LENGTH                       2048
#define WEBSOCKET_RX_BUFFER_LENGTH                  ( 12 * 1024 )

/*----------------------------------------------------------------------------*/

#define ISO8601_TIME_LENGTH                 17
#define REQUIRED_HEADER_USER_AGENT_IDX      0
#define REQUIRED_HEADER_ISO8601_TIME_IDX    1
#define REQUIRED_HEADER_SESSION_TOKEN_IDX   2
#define REQUIRED_HEADER_AUTHORIZATION_IDX   3
#define NUM_REQUIRED_HEADERS                4

/*----------------------------------------------------------------------------*/

typedef enum NetworkingResult
{
    NETWORKING_RESULT_OK,
    NETWORKING_RESULT_FAIL,
    NETWORKING_RESULT_BAD_PARAM,
} NetworkingResult_t;

typedef enum HttpVerb
{
    HTTP_GET,
    HTTP_POST,
} HttpVerb_t;

/*----------------------------------------------------------------------------*/

typedef struct SSLCredentials
{
    const char * pCaCertPath;
    const char * pDeviceCertPath;
    const char * pDeviceKeyPath;
} SSLCredentials_t;

typedef struct AwsCredentials
{
    const char * pAccessKeyId;
    size_t accessKeyIdLen;

    const char * pSecretAccessKey;
    size_t secretAccessKeyLen;

    char * pSessionToken;
    size_t sessionTokenLength;

    uint64_t expirationSeconds;
} AwsCredentials_t;

typedef struct AwsConfig
{
    const char * pRegion;
    size_t regionLen;

    const char * pService;
    size_t serviceLen;
} AwsConfig_t;

typedef struct HttpRequestHeader
{
    const char * pName;
    const char * pValue;
    size_t valueLength;
} HttpRequestHeader_t;

typedef struct HttpRequest
{
    HttpVerb_t verb;
    const char * pUrl;
    size_t urlLength;
    HttpRequestHeader_t * pHeaders;
    size_t numHeaders;
    const char * pUserAgent;
    size_t userAgentLength;
    const char * pBody;
    size_t bodyLength;
} HttpRequest_t;

typedef struct HttpResponse
{
    char * pContent;
    size_t contentLength;
    size_t contentMaxCapacity;
} HttpResponse_t;

typedef int ( * WebsocketMessageReceivedCallback_t )( char * pMessage,
                                                      size_t messageLength,
                                                      void * pUserData );
typedef struct WebsocketConnectInfo
{
    char * pUrl;
    size_t urlLength;
    WebsocketMessageReceivedCallback_t rxCallback;
    void * pRxCallbackData;
} WebsocketConnectInfo_t;

typedef struct NetworkingHttpContext
{
    struct lws_context * pLwsContext;
    struct lws_protocols protocols[ 2 ];

    /* Current time in ISO8601 format. */
    char iso8601Time[ ISO8601_TIME_LENGTH ];
    size_t iso8601TimeLength;

    /* Host portion of the URI. */
    char uriHost[ HTTP_URI_HOST_BUFFER_LENGTH + 1 ];
    size_t uriHostLength;

    /* Path portion of the URI. */
    char uriPath[ HTTP_URI_PATH_BUFFER_LENGTH + 1 ];
    size_t uriPathLength;

    /* Authorization added to the request. */
    char sigv4AuthorizationHeader[ SIGV4_AUTHORIZATION_HEADER_BUFFER_LENGTH ];
    size_t sigv4AuthorizationHeaderLength;

    /* Used in SigV4 calculation. */
    char sigV4Metadata[ SIGV4_METADATA_BUFFER_LENGTH ];

    HttpRequestHeader_t requiredHeaders[ NUM_REQUIRED_HEADERS ];
    HttpRequest_t * pRequest;
    HttpResponse_t * pResponse;
    char rxBuffer[ HTTP_RX_BUFFER_LENGTH ];
    uint8_t connectionClosed;
    int httpStatusCode;
} NetworkingHttpContext_t;

typedef struct NetworkingWebsocketContext
{
    struct lws_context * pLwsContext;
    struct lws * pWsi;
    struct lws_protocols protocols[ 2 ];
    struct lws_context_creation_info creationInfo;
    lws_retry_bo_t retryPolicy;

    /* Current time in ISO8601 format. */
    char iso8601Time[ ISO8601_TIME_LENGTH ];
    size_t iso8601TimeLength;

    /* Host portion of the URI. */
    char uriHost[ WEBSOCKET_URI_HOST_BUFFER_LENGTH + 1 ];
    size_t uriHostLength;

    /* Path portion of the URI. */
    char uriPath[ WEBSOCKET_URI_PATH_BUFFER_LENGTH + 1 ];
    size_t uriPathLength;

    /* Authorization added to the request. */
    char sigv4AuthorizationHeader[ SIGV4_AUTHORIZATION_HEADER_BUFFER_LENGTH ];
    size_t sigv4AuthorizationHeaderLength;

    /* Used in SigV4 calculation. */
    char sigV4Metadata[ SIGV4_METADATA_BUFFER_LENGTH ];
    WebsocketMessageReceivedCallback_t rxCallback;
    void * pRxCallbackData;
    char rxBuffer[ WEBSOCKET_RX_BUFFER_LENGTH ];
    size_t dataLengthInRxBuffer;
    uint8_t connectionEstablished;
    uint8_t connectionClosed;
    uint8_t connectionCloseRequested;
    RingBuffer_t ringBuffer;
} NetworkingWebsocketContext_t;

/*----------------------------------------------------------------------------*/

NetworkingResult_t Networking_HttpInit( NetworkingHttpContext_t * pHttpCtx,
                                        const SSLCredentials_t * pCreds );

NetworkingResult_t Networking_WebsocketInit( NetworkingWebsocketContext_t * pWebsocketCtx,
                                             const SSLCredentials_t * pCreds );

NetworkingResult_t Networking_HttpSend( NetworkingHttpContext_t * pHttpCtx,
                                        HttpRequest_t * pRequest,
                                        const AwsCredentials_t * pAwsCredentials,
                                        const AwsConfig_t * pAwsConfig,
                                        HttpResponse_t * pResponse );

NetworkingResult_t Networking_WebsocketConnect( NetworkingWebsocketContext_t * pWebsocketCtx,
                                                const WebsocketConnectInfo_t * pConnectInfo,
                                                const AwsCredentials_t * pAwsCredentials,
                                                const AwsConfig_t * pAwsConfig );

NetworkingResult_t Networking_WebsocketDisconnect( NetworkingWebsocketContext_t * pWebsocketCtx );

NetworkingResult_t Networking_WebsocketSend( NetworkingWebsocketContext_t * pWebsocketCtx,
                                             const char * pMessage,
                                             size_t messageLength );

NetworkingResult_t Networking_WebsocketSignal( NetworkingWebsocketContext_t * pWebsocketCtx );

/*----------------------------------------------------------------------------*/
