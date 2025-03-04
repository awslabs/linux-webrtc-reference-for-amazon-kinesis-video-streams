#ifndef SIGNALING_CONTROLLER_H
#define SIGNALING_CONTROLLER_H

#include <pthread.h>

#include "signaling_api.h"
#include "networking.h"

/* Config parameters. TODO aggarg - need to move to a central config file. */
#define SIGNALING_CONTROLLER_FETCH_CREDS_GRACE_PERIOD_SEC           ( 30 )
#define SIGNALING_CONTROLLER_HTTP_URL_BUFFER_LENGTH                 ( 1024 )
#define SIGNALING_CONTROLLER_HTTP_BODY_BUFFER_LENGTH                ( 10 * 1024 )
#define SIGNALING_CONTROLLER_HTTP_RESPONSE_BUFFER_LENGTH            ( 10 * 1024 )
#define SIGNALING_CONTROLLER_MESSAGE_BUFFER_LENGTH                  ( 10 * 1024 )
#define SIGNALING_CONTROLLER_HTTP_NUM_RETRIES                       ( 5U )
#define SIGNALING_CONTROLLER_ARN_BUFFER_LENGTH                      ( 128 )
#define SIGNALING_CONTROLLER_ENDPOINT_BUFFER_LENGTH                 ( 128 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT              ( 3 )
#define SIGNALING_CONTROLLER_ICE_SERVER_URI_BUFFER_LENGTH           ( 256 )
#define SIGNALING_CONTROLLER_ICE_SERVER_USER_NAME_BUFFER_LENGTH     ( 256 )
#define SIGNALING_CONTROLLER_ICE_SERVER_PASSWORD_BUFFER_LENGTH      ( 256 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_CONFIG_COUNT            ( 5 )
#define SIGNALING_CONTROLLER_ICE_CONFIG_REFRESH_GRACE_PERIOD_SEC    ( 30 )

/*----------------------------------------------------------------------------*/

typedef enum SignalingControllerResult
{
    SIGNALING_CONTROLLER_RESULT_OK = 0,
    SIGNALING_CONTROLLER_RESULT_BAD_PARAM,
    SIGNALING_CONTROLLER_RESULT_FAIL
} SignalingControllerResult_t;

typedef struct SignalingMessage
{
    const char * pRemoteClientId;
    size_t remoteClientIdLength;
    SignalingTypeMessage_t messageType;
    const char * pMessage;
    size_t messageLength;
    const char * pCorrelationId;
    size_t correlationIdLength;
} SignalingMessage_t;

typedef int ( * SignalingMessageReceivedCallback_t )( SignalingMessage_t * pSignalingMessage,
                                                      void * pUserData );

typedef struct AwsIotCredentials
{
    char * pThingName;
    size_t thingNameLength;
    char * pRoleAlias;
    size_t roleAliasLength;
    char * pIotCredentialsEndpoint;
    size_t iotCredentialsEndpointLength;
} AwsIotCredentials_t;

typedef struct SignalingControllerConnectInfo
{
    AwsCredentials_t awsCreds;
    AwsConfig_t awsConfig;
    AwsIotCredentials_t awsIotCreds;
    SignalingChannelName_t channelName;
    const char * pUserAgentName;
    size_t userAgentNameLength;
    SignalingMessageReceivedCallback_t messageReceivedCallback;
    void * pMessageReceivedCallbackData;
} SignalingControllerConnectInfo_t;

typedef struct IceServerUri
{
    char uri[ SIGNALING_CONTROLLER_ICE_SERVER_URI_BUFFER_LENGTH + 1 ];
    size_t uriLength;
} IceServerUri_t;

typedef struct IceServerConfig
{
    uint32_t ttlSeconds;
    IceServerUri_t iceServerUris[ SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT ];
    size_t iceServerUriCount;
    char userName[ SIGNALING_CONTROLLER_ICE_SERVER_USER_NAME_BUFFER_LENGTH + 1];
    size_t userNameLength;
    char password[ SIGNALING_CONTROLLER_ICE_SERVER_PASSWORD_BUFFER_LENGTH + 1];
    size_t passwordLength;
} IceServerConfig_t;

typedef struct SignalingControllerContext
{
    char accessKeyId[ ACCESS_KEY_MAX_LEN + 1 ];
    size_t accessKeyIdLength;
    char secretAccessKey[ SECRET_ACCESS_KEY_MAX_LEN + 1 ];
    size_t secretAccessKeyLength;
    char sessionToken[ SESSION_TOKEN_MAX_LEN + 1 ];
    size_t sessionTokenLength;
    uint64_t expirationSeconds;

    char signalingChannelArn[ SIGNALING_CONTROLLER_ARN_BUFFER_LENGTH + 1 ];
    size_t signalingChannelArnLength;

    char wssEndpoint[ SIGNALING_CONTROLLER_ENDPOINT_BUFFER_LENGTH  + 1 ];
    size_t wssEndpointLength;
    char httpsEndpoint[ SIGNALING_CONTROLLER_ENDPOINT_BUFFER_LENGTH  + 1 ];
    size_t httpsEndpointLength;
    char webrtcEndpoint[ SIGNALING_CONTROLLER_ENDPOINT_BUFFER_LENGTH + 1 ];
    size_t webrtcEndpointLength;

    uint64_t iceServerConfigExpirationSec;
    size_t iceServerConfigsCount;
    IceServerConfig_t iceServerConfigs[ SIGNALING_CONTROLLER_ICE_SERVER_MAX_CONFIG_COUNT ];

    const char * pUserAgentName;
    size_t userAgentNameLength;

    AwsConfig_t awsConfig;

    char httpUrlBuffer[ SIGNALING_CONTROLLER_HTTP_URL_BUFFER_LENGTH ];
    char httpBodyBuffer[ SIGNALING_CONTROLLER_HTTP_BODY_BUFFER_LENGTH ];
    char httpResponserBuffer[ SIGNALING_CONTROLLER_HTTP_RESPONSE_BUFFER_LENGTH ];
    char signalingRxMessageBuffer[ SIGNALING_CONTROLLER_MESSAGE_BUFFER_LENGTH ];
    size_t signalingRxMessageLength;
    char signalingTxMessageBuffer[ SIGNALING_CONTROLLER_MESSAGE_BUFFER_LENGTH ];
    size_t signalingTxMessageLength;
    char signalingIntermediateMessageBuffer[ SIGNALING_CONTROLLER_MESSAGE_BUFFER_LENGTH ];
    size_t signalingIntermediateMessageLength;

    /* Serialize access to SignalingController_SendMessage. */
    pthread_mutex_t signalingTxMutex;

    SignalingMessageReceivedCallback_t messageReceivedCallback;
    void * pMessageReceivedCallbackData;

    NetworkingHttpContext_t httpContext;
    NetworkingWebsocketContext_t websocketContext;
} SignalingControllerContext_t;

/*----------------------------------------------------------------------------*/

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t * pCtx,
                                                      const SSLCredentials_t * pSslCreds );

/* Start listening for incoming SDP offers. */
SignalingControllerResult_t SignalingController_StartListening( SignalingControllerContext_t * pCtx,
                                                                const SignalingControllerConnectInfo_t * pConnectInfo );

SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t * pCtx,
                                                             const SignalingMessage_t * pSignalingMessage );

SignalingControllerResult_t SignalingController_QueryIceServerConfigs( SignalingControllerContext_t * pCtx,
                                                                       IceServerConfig_t ** ppIceServerConfigs,
                                                                       size_t * pIceServerConfigsCount );

SignalingControllerResult_t SignalingController_RefreshIceServerConfigs( SignalingControllerContext_t * pCtx );

SignalingControllerResult_t SignalingController_ExtractSdpOfferFromSignalingMessage( const char * pSignalingMessage,
                                                                                     size_t signalingMessageLength,
                                                                                     const char ** ppSdpMessage,
                                                                                     size_t * pSdpMessageLength );

SignalingControllerResult_t SignalingController_DeserializeSdpContentNewline( const char * pSdpMessage,
                                                                              size_t sdpMessageLength,
                                                                              char * pFormalSdpMessage,
                                                                              size_t * pFormalSdpMessageLength );

SignalingControllerResult_t SignalingController_SerializeSdpContentNewline( const char * pSdpMessage,
                                                                            size_t sdpMessageLength,
                                                                            char * pEventSdpMessage,
                                                                            size_t * pEventSdpMessageLength );

/*----------------------------------------------------------------------------*/

#endif /* SIGNALING_CONTROLLER_H */
