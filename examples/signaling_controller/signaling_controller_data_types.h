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
#include "networkingLibwebsockets.h"

/* Refer to https://docs.aws.amazon.com/IAM/latest/APIReference/API_AccessKey.html,
   length of access key ID should be limited to 128. There is no other definition of
   length of secret access key, set it same as access key ID for now. */
#define SIGNALING_CONTROLLER_ACCESS_KEY_ID_MAX_LENGTH ( 128 )
#define SIGNALING_CONTROLLER_SECRET_ACCESS_KEY_MAX_LENGTH ( 128 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT ( SIGNALING_AWS_ICE_SERVER_MAX_NUM )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT ( SIGNALING_AWS_ICE_SERVER_MAX_URIS )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_URI_LENGTH ( 256 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_USER_NAME_LENGTH ( 256 )
#define SIGNALING_CONTROLLER_ICE_SERVER_MAX_PASSWORD_LENGTH ( 256 )
#define SIGNALING_CONTROLLER_DECODED_BUFFER_LENGTH ( 10000 )

typedef struct SignalingControllerReceiveEvent
{
    const char *pSenderClientId;
    size_t senderClientIdLength;
    SignalingTypeMessage_t messageType;
    const char *pDecodeMessage;
    size_t decodeMessageLength;
    const char *pCorrelationId;
    size_t correlationIdLength;
} SignalingControllerReceiveEvent_t;

typedef int32_t (*SignalingControllerReceiveMessageCallback)( SignalingControllerReceiveEvent_t *pEvent, void *pUserContext );

typedef enum SignalingControllerResult
{
    SIGNALING_CONTROLLER_RESULT_OK = 0,
    SIGNALING_CONTROLLER_RESULT_FAIL,
    SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER,
    SIGNALING_CONTROLLER_RESULT_SIGNALING_INIT_FAIL,
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
    SIGNALING_CONTROLLER_RESULT_INACTIVE_SIGNALING_CHANNEL,
    SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_ARN,
    SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_NAME,
    SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_URI,
    SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_USERNAME,
    SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_PASSWORD,
    SIGNALING_CONTROLLER_RESULT_WEBSOCKET_INIT_FAIL,
    SIGNALING_CONTROLLER_RESULT_WSS_CONNECT_FAIL,
    SIGNALING_CONTROLLER_RESULT_WSS_RECV_FAIL,
} SignalingControllerResult_t;

typedef struct SignalingControllerCredential
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

    /* CA Cert Path */
    char * pCaCertPath;

    /* TODO: Or credential */
} SignalingControllerCredential_t;

typedef struct SignalingControllerChannelInfo
{
    /* Describe signaling channel */
    char signalingChannelName[SIGNALING_AWS_MAX_CHANNEL_NAME_LEN + 1];
    size_t signalingChannelNameLength;
    char signalingChannelARN[SIGNALING_AWS_MAX_ARN_LEN + 1];
    size_t signalingChannelARNLength;
    uint32_t signalingChannelTtlSeconds;

    /* Get signaling channel endpoints */
    char endpointWebsocketSecure[SIGNALING_AWS_MAX_ARN_LEN + 1];
    size_t endpointWebsocketSecureLength;
    char endpointHttps[SIGNALING_AWS_MAX_ARN_LEN + 1];
    size_t endpointHttpsLength;
    char endpointWebrtc[SIGNALING_AWS_MAX_ARN_LEN + 1];
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

typedef struct SignalingControllerMetrics
{
    struct timeval describeSignalingChannelStartTime;
    struct timeval describeSignalingChannelEndTime;
    struct timeval getSignalingEndpointsStartTime;
    struct timeval getSignalingEndpointsEndTime;
    struct timeval getIceServerListStartTime;
    struct timeval getIceServerListEndTime;
    struct timeval connectWssServerStartTime;
    struct timeval connectWssServerEndTime;
} SignalingControllerMetrics_t;

typedef struct SignalingControllerContext
{
    /* Signaling Component Context */
    SignalingContext_t signalingContext;

    SignalingControllerCredential_t credential;

    SignalingControllerChannelInfo_t channelInfo;

    uint8_t iceServerConfigsCount;
    SignalingControllerIceServerConfig_t iceServerConfigs[SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT];

    SignalingControllerMetrics_t metrics;

    SignalingControllerReceiveMessageCallback receiveMessageCallback;
    void *pReceiveMessageCallbackContext;
    char decodeBuffer[ SIGNALING_CONTROLLER_DECODED_BUFFER_LENGTH ];
    size_t decodeBufferLength;
} SignalingControllerContext_t;

#ifdef __cplusplus
}
#endif

#endif /* SIGNALING_CONTROLLER_DATA_TYPES_H */
