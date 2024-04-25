#ifndef DEMO_DATA_TYPES_H
#define DEMO_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "sdp_controller.h"
#include "signaling_controller.h"
#include "ice_controller.h"

#define DEMO_SDP_BUFFER_MAX_LENGTH ( 10000 )

typedef struct DemoSessionInformation
{
    char sdpBuffer[ DEMO_SDP_BUFFER_MAX_LENGTH ];
    size_t sdpBufferLength;
    SdpControllerSdpDescription_t sdpDescription;
} DemoSessionInformation_t;

typedef struct DemoContext
{
    /* Signaling controller. */
    SignalingControllerContext_t signalingControllerContext;

    /* SDP buffers. */
    DemoSessionInformation_t sessionInformationSdpOffer;
    DemoSessionInformation_t sessionInformationSdpAnswer;
    char sdpConstructedBuffer[ DEMO_SDP_BUFFER_MAX_LENGTH ];
    size_t sdpConstructedBufferLength;

    /* Ice controller. */
    IceControllerContext_t iceControllerContext;
} DemoContext_t;

#ifdef __cplusplus
}
#endif

#endif /* DEMO_DATA_TYPES_H */