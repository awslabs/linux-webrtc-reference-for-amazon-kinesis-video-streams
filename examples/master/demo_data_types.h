#ifndef DEMO_DATA_TYPES_H
#define DEMO_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "sdp_controller.h"

#define DEMO_SDP_OFFER_BUFFER_MAX_LENGTH ( 10000 )

typedef struct DemoSessionInformation
{
    char sdpOfferBuffer[ DEMO_SDP_OFFER_BUFFER_MAX_LENGTH ];
    size_t sdpOfferBufferLength;
    SdpControllerSdpOffer_t sdpOffer;
} DemoSessionInformation_t;

typedef struct DemoContext
{
    DemoSessionInformation_t sessionInformation;
} DemoContext_t;

#ifdef __cplusplus
}
#endif

#endif /* DEMO_DATA_TYPES_H */