#ifndef SIGNALING_CONTROLLER_DATA_TYPES_H
#define SIGNALING_CONTROLLER_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Standard includes. */
#include <stdint.h>
#include <stddef.h>
#include "signaling_api.h"
#include "httpsLibwebsockets.h"

/* Refer to https://docs.aws.amazon.com/IAM/latest/APIReference/API_AccessKey.html,
   length of access key ID should be limited to 128. There is no other definition of
   length of secret access key, set it same as access key ID for now. */
#define SIGNALING_CONTROLLER_ACCESS_KEY_ID_MAX_LENGTH ( 128 )
#define SIGNALING_CONTROLLER_SECRET_ACCESS_KEY_MAX_LENGTH ( 128 )

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
    SIGNALING_CONTROLLER_RESULT_INVALID_HTTPS_ENDPOINT,
    SIGNALING_CONTROLLER_RESULT_INVALID_WEBSOCKET_SECURE_ENDPOINT,
    SIGNALING_CONTROLLER_RESULT_INVALID_WEBRTC_ENDPOINT,
    SIGNALING_CONTROLLER_RESULT_HTTPS_INIT_FAIL,
    SIGNALING_CONTROLLER_RESULT_HTTPS_PERFORM_REQUEST_FAIL,
    SIGNALING_CONTROLLER_RESULT_INACTIVE_SIGNALING_CHANNEL,
    SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_ARN,
    SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_NAME,
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

typedef struct SignalingControllerContext
{
    /* Signaling Component Context */
    SignalingContext_t signalingContext;

    SignalingControllerCredential_t signalingControllerCredential;

    SignalingControllerChannelInfo_t signalingChannelInfo;

    /* HTTPS Context */
    HttpsContext_t httpsContext;
} SignalingControllerContext_t;

#ifdef __cplusplus
}
#endif

#endif /* SIGNALING_CONTROLLER_DATA_TYPES_H */
