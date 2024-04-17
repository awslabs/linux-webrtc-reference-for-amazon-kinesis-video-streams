#ifndef SDP_CONTROLLER_H
#define SDP_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

typedef enum SdpControllerResult
{
    SDP_CONTROLLER_RESULT_OK = 0,
    SDP_CONTROLLER_RESULT_BAD_PARAMETER,
} SdpControllerResult_t;

SdpControllerResult_t SdpController_DeserializeSdpOffer( char *pSdpMessage, size_t sdpMessageLength );

#ifdef __cplusplus
}
#endif

#endif /* SDP_CONTROLLER_H */