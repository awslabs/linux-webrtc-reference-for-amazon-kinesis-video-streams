#ifndef SIGNALING_CONTROLLER_DATA_TYPES_H
#define SIGNALING_CONTROLLER_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Standard includes. */
#include <stdint.h>
#include <stddef.h>
#include "sigv4.h"
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
    SIGNALING_CONTROLLER_RESULT_SIGNALING_DESCRIBE_SIGNALING_CHANNEL_FAIL,
    SIGNALING_CONTROLLER_RESULT_HTTPS_INIT_FAIL,
} SignalingControllerResult_t;

typedef struct SignalingControllerCredential
{
    /* Region */
    char * pRegion;
    size_t regionLength;

    /* Channel Name */
    char * pChannelName;
    size_t channelNameLength;

    /* AKSK */
    char * pAccessKeyId;
    size_t accessKeyIdLength;
    char * pSecretAccessKey;
    size_t secretAccessKeyLength;

    /* TODO: Or credential */
} SignalingControllerCredential_t;

typedef struct SignalingControllerContext
{
    /* Region */
    char * pRegion;
    size_t regionLength;

    /* Channel Name */
    char * pChannelName;
    size_t channelNameLength;

    /* SigV4 credential */
    SigV4Credentials_t credential;

    /* Signaling Component Context */
    SignalingContext_t signalingContext;

    /* HTTPS Context */
    HttpsContext_t httpsContext;
} SignalingControllerContext_t;

#ifdef __cplusplus
}
#endif

#endif /* SIGNALING_CONTROLLER_DATA_TYPES_H */
