/* Standard includes. */
#include <stdlib.h>

/* LWS includes. */
#include "libwebsockets.h"

/* Ring buffer includes. */
#include "ring_buffer.h"

/* Logging includes. */
#include "logging.h"

/* Config parameters. TODO aggarg - need to move to a central config file. */
#define URI_HOST_BUFFER_LENGTH                      128
#define URI_PATH_BUFFER_LENGTH                      2048
#define SIGV4_METADATA_BUFFER_LENGTH                4096
#define SIGV4_AUTHORIZATION_HEADER_BUFFER_LENGTH    2048
#define HTTP_RX_BUFFER_LENGTH                       2048
#define WEBSOCKET_RX_BUFFER_LENGTH                  ( 10 * 1024 )

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

    const char * pRegion;
    size_t regionLen;

    const char * pService;
    size_t serviceLen;

    char * pSessionToken;
    size_t sessionTokenLength;

    uint64_t expirationSeconds;
} AwsCredentials_t;

typedef struct HttpRequestHeader
{
    const char * pName;
    const char * pValue;
    size_t valueLength;
} HttpRequestHeader_t;

typedef struct HttpRequest
{
    HttpVerb_t verb;
    char * pUrl;
    size_t urlLength;
    HttpRequestHeader_t * pHeaders;
    size_t numHeaders;
    char * pUserAgent;
    size_t userAgentLength;
    char * pBody;
    size_t bodyLength;
} HttpRequest_t;

typedef struct HttpResponse
{
    char * pBuffer;
    size_t bufferLength;
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

typedef struct HttpContext
{
    HttpRequestHeader_t requiredHeaders[ NUM_REQUIRED_HEADERS ];
    HttpRequest_t * pRequest;
    HttpResponse_t * pResponse;
    char rxBuffer[ HTTP_RX_BUFFER_LENGTH ];
    uint8_t connectionClosed;
} HttpContext_t;

typedef struct WebsocketContext
{
    WebsocketMessageReceivedCallback_t rxCallback;
    void * pRxCallbackData;
    char rxBuffer[ WEBSOCKET_RX_BUFFER_LENGTH ];
    size_t dataLengthInRxBuffer;
    uint8_t connectionEstablished;
    uint8_t connectionClosed;
    RingBuffer_t ringBuffer;
    struct lws * pWsi;
} WebsocketContext_t;

typedef struct NetworkingContext
{
    struct lws_context * pLwsContext;
    struct lws_protocols protocols[ 3 ];

    /* Current time in ISO8601 format. */
    char iso8601Time[ ISO8601_TIME_LENGTH ];
    size_t iso8601TimeLength;

    /* Host portion of the URI. */
    char uriHost[ URI_HOST_BUFFER_LENGTH + 1 ];
    size_t uriHostLength;

    /* Path portion of the URI. */
    char uriPath[ URI_PATH_BUFFER_LENGTH + 1 ];
    size_t uriPathLength;

    /* Authorization added to the request. */
    char sigv4AuthorizationHeader[ SIGV4_AUTHORIZATION_HEADER_BUFFER_LENGTH ];
    size_t sigv4AuthorizationHeaderLength;

    /* Used in SigV4 calculation. */
    char sigV4Metadata[ SIGV4_METADATA_BUFFER_LENGTH ];

    HttpContext_t httpContext;
    WebsocketContext_t websocketContext;
} NetworkingContext_t;

/*----------------------------------------------------------------------------*/

NetworkingResult_t Networking_Init( NetworkingContext_t * pCtx,
                                    const SSLCredentials_t * pCreds );

NetworkingResult_t Networking_HttpSend( NetworkingContext_t * pCtx,
                                        HttpRequest_t * pRequest,
                                        const AwsCredentials_t * pAwsCredentials,
                                        HttpResponse_t * pResponse );

NetworkingResult_t Networking_WebsocketConnect( NetworkingContext_t * pCtx,
                                                const WebsocketConnectInfo_t * pConnectInfo,
                                                const AwsCredentials_t * pAwsCredentials );

NetworkingResult_t Networking_WebsocketSend( NetworkingContext_t * pCtx,
                                             const char * pMessage,
                                             size_t messageLength );

NetworkingResult_t Networking_WebsocketSignal( NetworkingContext_t * pCtx );

/*----------------------------------------------------------------------------*/
