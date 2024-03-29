#ifndef SIGNALING_CONTROLLER_DATA_TYPES_H
#define SIGNALING_CONTROLLER_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Standard includes. */
#include <stdint.h>
#include <stddef.h>

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
} SignalingControllerResult_t;

typedef struct SignalingControllerCredential
{
    /* AKSK */
    char * pAccessKeyId;
    size_t accessKeyIdLength;
    char * pSecretAccessKey;
    size_t secretAccessKeyLength;

    /* TODO: Or credential */
} SignalingControllerCredential_t;

typedef struct SignalingControllerContext
{
    /* AKSK */
    char accessKeyId[ SIGNALING_CONTROLLER_ACCESS_KEY_ID_MAX_LENGTH + 1 ];
    char secretAccessKey[ SIGNALING_CONTROLLER_SECRET_ACCESS_KEY_MAX_LENGTH + 1 ];

    /* TODO: Or credential */
} SignalingControllerContext_t;

#ifdef __cplusplus
}
#endif

#endif /* SIGNALING_CONTROLLER_DATA_TYPES_H */
