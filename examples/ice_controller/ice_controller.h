#ifndef ICE_CONTROLLER_H
#define ICE_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "ice_controller_data_types.h"

void getLocalIPAdresses( void );

IceControllerResult_t IceController_Init( IceControllerContext_t *pCtx );
IceControllerResult_t IceController_DeserializeIceCandidate( const char *pDecodeMessage, size_t decodeMessageLength, IceControllerCandidate_t *pCandidate );
IceControllerResult_t IceController_SetRemoteDescription(  );

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_H */
