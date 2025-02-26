#ifndef SIGNALING_CONTROLLER_DATA_TYPES_H
#define SIGNALING_CONTROLLER_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Standard includes. */
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "signaling_api.h"
#include "networking.h"

#define SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS ( 1 )
#define SIGNALING_CONTROLLER_USING_COREHTTP ( 0 )
#define SIGNALING_CONTROLLER_USING_WSLAY ( 0 )

/* Refer to https://docs.aws.amazon.com/IAM/latest/APIReference/API_AccessKey.html,
   length of access key ID should be limited to 128. There is no other definition of
   length of secret access key, set it same as access key ID for now. */
#define SIGNALING_CONTROLLER_ACCESS_KEY_ID_MAX_LENGTH ( ACCESS_KEY_MAX_LEN )
#define SIGNALING_CONTROLLER_SECRET_ACCESS_KEY_MAX_LENGTH ( SECRET_ACCESS_KEY_MAX_LEN )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT ( 5 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT ( 3 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_URI_LENGTH ( 256 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_USER_NAME_LENGTH ( 256 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_PASSWORD_LENGTH ( 256 )
#define SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH ( 10000 )
#define SIGNALING_CONTROLLER_CORRELATION_ID_MAX_LENGTH ( 256 )
#define SIGNALING_CONTROLLER_REMOTE_ID_MAX_LENGTH ( 256 )
#define SIGNALING_CONTROLLER_MAX_HTTP_URI_LENGTH ( 10000 )
#define SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH ( 10 * 1024 )
#define SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH ( 1024 )
#define SIGNALING_CONTROLLER_AWS_MAX_CHANNEL_NAME_LENGTH ( SIGNALING_CHANNEL_NAME_MAX_LEN )
#define SIGNALING_CONTROLLER_AWS_MAX_SESSION_TOKEN_LENGTH ( SESSION_TOKEN_MAX_LEN )
#define SIGNALING_CONTROLLER_AWS_MAX_EXPIRATION_LENGTH ( 2048 )
#define SIGNALING_CONTROLLER_AWS_REGION_MAX_LENGTH ( 50 )
#define SIGNALING_CONTROLLER_AWS_USER_AGENT_MAX_LENGTH ( 128 )

// Max path characters as defined in linux/limits.h
#define SIGNALING_CONTROLLER_MAX_PATH_LENGTH ( 2048 )

// Max size of certificate
#define SIGNALING_CONTROLLER_CERTIFICATE_MAX_LENGTH ( 2048 )

// Max URL length of AWS credential endpoint
#define SIGNALING_CONTROLLER_AWS_CRED_ENDPOINT_MAX_LENGTH ( 2048 )

/**
 * Maximum allowed string length for IoT thing name: https://docs.aws.amazon.com/iot/latest/apireference/API_CreateThing.html
 */
#define SIGNALING_CONTROLLER_AWS_IOT_THING_NAME_MAX_LENGTH ( 128 )

/**
 * Maximum allowed role alias length https://docs.aws.amazon.com/iot/latest/apireference/API_UpdateRoleAlias.html
 */
#define SIGNALING_CONTROLLER_AWS_IOT_ROLE_ALIAS_MAX_LENGTH ( 128 )

/**
 * Grace period for refetching the session token
 */
#define SIGNALING_CONTROLLER_FETCH_SESSION_TOKEN_GRACE_PERIOD_SEC ( 30 )

/**
 * Default connect sync API timeout
 */
#define SIGNALING_CONNECT_STATE_TIMEOUT_SEC ( 15 )

/**
 * Grace period for refreshing the ICE configuration
 */
#define ICE_CONFIGURATION_REFRESH_GRACE_PERIOD_SEC ( 30 )

typedef enum SignalingControllerEventStatus
{
    SIGNALING_CONTROLLER_EVENT_STATUS_NONE = 0,
    SIGNALING_CONTROLLER_EVENT_STATUS_SENT_DONE,
    SIGNALING_CONTROLLER_EVENT_STATUS_SENT_FAIL,
} SignalingControllerEventStatus_t;

typedef struct SignalingControllerReceiveEvent
{
    const char * pRemoteClientId;
    size_t remoteClientIdLength;
    SignalingTypeMessage_t messageType;
    const char * pDecodeMessage;
    size_t decodeMessageLength;
    const char * pCorrelationId;
    size_t correlationIdLength;
} SignalingControllerReceiveEvent_t;

typedef struct SignalingControllerEventContentSend
{
    char remoteClientId[ SIGNALING_CONTROLLER_REMOTE_ID_MAX_LENGTH ];
    size_t remoteClientIdLength;
    SignalingTypeMessage_t messageType;
    char * pDecodeMessage;
    size_t decodeMessageLength;
    char correlationId[ SIGNALING_CONTROLLER_CORRELATION_ID_MAX_LENGTH ];
    size_t correlationIdLength;
} SignalingControllerEventContentSend_t;

typedef int32_t (* SignalingControllerReceiveMessageCallback)( SignalingControllerReceiveEvent_t * pEvent,
                                                               void * pUserContext );
typedef int32_t (* SignalingControllerCompleteSendCallback)( SignalingControllerEventStatus_t status,
                                                             void * pUserContext );

typedef enum SignalingControllerResult
{
    SIGNALING_CONTROLLER_RESULT_OK = 0,
    SIGNALING_CONTROLLER_RESULT_FAIL,
    SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER,
    SIGNALING_CONTROLLER_RESULT_CONSTRUCT_DESCRIBE_SIGNALING_CHANNEL_FAIL,
    SIGNALING_CONTROLLER_RESULT_PARSE_DESCRIBE_SIGNALING_CHANNEL_FAIL,
    SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL,
    SIGNALING_CONTROLLER_RESULT_PARSE_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL,
    SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_SERVER_LIST_FAIL,
    SIGNALING_CONTROLLER_RESULT_PARSE_GET_SIGNALING_SERVER_LIST_FAIL,
    SIGNALING_CONTROLLER_RESULT_INVALID_HTTP_ENDPOINT,
    SIGNALING_CONTROLLER_RESULT_INVALID_WEBSOCKET_SECURE_ENDPOINT,
    SIGNALING_CONTROLLER_RESULT_INVALID_WEBRTC_ENDPOINT,
    SIGNALING_CONTROLLER_RESULT_HTTP_INIT_FAIL,
    SIGNALING_CONTROLLER_RESULT_HTTP_PERFORM_REQUEST_FAIL,
    SIGNALING_CONTROLLER_RESULT_HTTP_UPDATE_FAIL,
    SIGNALING_CONTROLLER_RESULT_INACTIVE_SIGNALING_CHANNEL,
    SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_ARN,
    SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_NAME,
    SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_URI,
    SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_USERNAME,
    SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_PASSWORD,
    SIGNALING_CONTROLLER_RESULT_INVALID_JSON,
    SIGNALING_CONTROLLER_RESULT_SDP_BUFFER_TOO_SMALL,
    SIGNALING_CONTROLLER_RESULT_SDP_NOT_TARGET_TYPE,
    SIGNALING_CONTROLLER_RESULT_SDP_SNPRINTF_FAIL,
    SIGNALING_CONTROLLER_RESULT_WEBSOCKET_INIT_FAIL,
    SIGNALING_CONTROLLER_RESULT_WEBSOCKET_UPDATE_FAIL,
    SIGNALING_CONTROLLER_RESULT_WSS_CONNECT_FAIL,
    SIGNALING_CONTROLLER_RESULT_WSS_RECV_FAIL,
    SIGNALING_CONTROLLER_RESULT_MQ_INIT_FAIL,
    SIGNALING_CONTROLLER_RESULT_MQ_SEND_FAIL,
    SIGNALING_CONTROLLER_RESULT_BASE64_ENCODE_FAIL,
    SIGNALING_CONTROLLER_RESULT_CONSTRUCT_SIGNALING_MSG_FAIL,
    SIGNALING_CONTROLLER_RESULT_PARSE_TEMPORARY_CREDENTIALS_FAIL,
} SignalingControllerResult_t;

typedef struct SignalingControllerCredentialInfo
{
    /* Region */
    char * pRegion;
    size_t regionLength;

    /* Channel Name */
    char * pChannelName;
    size_t channelNameLength;

    /* User Agent Name */
    char * pUserAgentName;
    size_t userAgentNameLength;

    /* AKSK */
    char * pAccessKeyId;
    size_t accessKeyIdLength;
    char * pSecretAccessKey;
    size_t secretAccessKeyLength;

    /* Session Token */
    char * pSessionToken;
    size_t sessionTokenLength;

    /* CA Cert Path */
    char * pCaCertPath;
    size_t caCertPathLength;
    char * pCaCertPem;
    size_t caCertPemSize;

    /* Credential Endpoint */
    char * pCredEndpoint;
    size_t credEndpointLength;

    /* AWS IoT Thing name */
    char * pIotThingName;
    size_t iotThingNameLength;

    /* AWS IoT Thing Role Alias name */
    char * pIotThingRoleAlias;
    size_t iotThingRoleAliasLength;

    /* AWS IoT Thing certificate path */
    char * pIotThingCertPath;
    size_t iotThingCertPathLength;

    /* AWS IoT Thing private key path */
    char * pIotThingPrivateKeyPath;
    size_t iotThingPrivateKeyPathLength;
} SignalingControllerCredentialInfo_t;

typedef struct SignalingControllerCredential
{
    /* Region */
    char region[ SIGNALING_CONTROLLER_AWS_REGION_MAX_LENGTH + 1 ];
    size_t regionLength;

    /* Channel Name */
    char channelName[ SIGNALING_CONTROLLER_AWS_MAX_CHANNEL_NAME_LENGTH + 1 ];
    size_t channelNameLength;

    /* User Agent Name */
    char userAgentName[ SIGNALING_CONTROLLER_AWS_USER_AGENT_MAX_LENGTH + 1 ];
    size_t userAgentNameLength;

    /* AKSK */
    char accessKeyId[ SIGNALING_CONTROLLER_ACCESS_KEY_ID_MAX_LENGTH + 1 ];
    size_t accessKeyIdLength;
    char secretAccessKey[ SIGNALING_CONTROLLER_SECRET_ACCESS_KEY_MAX_LENGTH + 1 ];
    size_t secretAccessKeyLength;

    /* Session Token */
    char sessionToken[ SIGNALING_CONTROLLER_AWS_MAX_SESSION_TOKEN_LENGTH + 1 ];
    size_t sessionTokenLength;

    /* Expiration */
    uint64_t expirationSeconds;

    /* CA Cert Path */
    char caCertPath[ SIGNALING_CONTROLLER_MAX_PATH_LENGTH + 1 ];
    size_t caCertPathLength;
    char caCertPem[ SIGNALING_CONTROLLER_CERTIFICATE_MAX_LENGTH + 1 ];
    size_t caCertPemSize;

    /* Credential Endpoint */
    char credEndpoint[ SIGNALING_CONTROLLER_AWS_CRED_ENDPOINT_MAX_LENGTH + 1 ];
    size_t credEndpointLength;

    /* AWS IoT Thing name */
    char iotThingName[ SIGNALING_CONTROLLER_AWS_IOT_THING_NAME_MAX_LENGTH + 1 ];
    size_t iotThingNameLength;

    /* AWS IoT Thing Role Alias name */
    char iotThingRoleAlias[ SIGNALING_CONTROLLER_AWS_IOT_ROLE_ALIAS_MAX_LENGTH + 1 ];
    size_t iotThingRoleAliasLength;

    /* AWS IoT Thing certificate path */
    char iotThingCertPath[ SIGNALING_CONTROLLER_MAX_PATH_LENGTH + 1 ];
    size_t iotThingCertPathLength;

    /* AWS IoT Thing private key path */
    char iotThingPrivateKeyPath[ SIGNALING_CONTROLLER_MAX_PATH_LENGTH + 1 ];
    size_t iotThingPrivateKeyPathLength;
} SignalingControllerCredential_t;

typedef struct SignalingControllerChannelInfo
{
    /* Describe signaling channel */
    char signalingChannelName[SIGNALING_CONTROLLER_AWS_MAX_CHANNEL_NAME_LENGTH + 1];
    size_t signalingChannelNameLength;
    char signalingChannelARN[SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH + 1];
    size_t signalingChannelARNLength;
    uint32_t signalingChannelTtlSeconds;

    /* Get signaling channel endpoints */
    char endpointWebsocketSecure[SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH + 1];
    size_t endpointWebsocketSecureLength;
    char endpointHttps[SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH + 1];
    size_t endpointHttpsLength;
    char endpointWebrtc[SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH + 1];
    size_t endpointWebrtcLength;
} SignalingControllerChannelInfo_t;

typedef struct SignalingControllerIceServerConfig
{
    uint32_t ttlSeconds;                                                                                            //!< TTL in seconds
    uint8_t uriCount;                                                                                               //!<  Number of Ice URI objects
    char uris[SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT][SIGNALING_CONTROLLER_ICE_SERVER_MAX_URI_LENGTH + 1];  //!< List of Ice server URIs
    size_t urisLength[SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT];                                              //!< Length of every URI
    char userName[SIGNALING_CONTROLLER_ICE_SERVER_MAX_USER_NAME_LENGTH + 1];                                        //!< Username for the server
    size_t userNameLength;                                                                                          //!< Length of username
    char password[SIGNALING_CONTROLLER_ICE_SERVER_MAX_PASSWORD_LENGTH + 1];                                         //!< Password for the server
    size_t passwordLength;                                                                                          //!< Length of password
} SignalingControllerIceServerConfig_t;

typedef enum SignalingControllerEvent
{
    SIGNALING_CONTROLLER_EVENT_NONE = 0,
    SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE,
    SIGNALING_CONTROLLER_EVENT_RECONNECT_WSS,
} SignalingControllerEvent_t;

typedef struct SignalingControllerEventMessage
{
    SignalingControllerEvent_t event;
    SignalingControllerEventContentSend_t eventContent;
    SignalingControllerCompleteSendCallback onCompleteCallback;
    void * pOnCompleteCallbackContext;
} SignalingControllerEventMessage_t;

typedef struct SignalingControllerContext
{
    SignalingControllerCredential_t credential;

    SignalingControllerChannelInfo_t channelInfo;

    uint64_t iceServerConfigExpirationSec;
    uint8_t iceServerConfigsCount;
    SignalingControllerIceServerConfig_t iceServerConfigs[SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT];

    SignalingControllerReceiveMessageCallback receiveMessageCallback;
    char httpUrlBuffer[ SIGNALING_CONTROLLER_MAX_HTTP_URI_LENGTH ];
    char httpBodyBuffer[ SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH ];
    char httpResponserBuffer[ SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH ];
    void * pReceiveMessageCallbackContext;
    char base64Buffer[ SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH ];
    size_t base64BufferLength;
    char constructedSignalingBuffer[ SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH ];
    size_t constructedSignalingBufferLength;

    NetworkingHttpContext_t httpContext;
    NetworkingWebsocketContext_t websocketContext;
} SignalingControllerContext_t;

#ifdef __cplusplus
}
#endif

#endif /* SIGNALING_CONTROLLER_DATA_TYPES_H */
