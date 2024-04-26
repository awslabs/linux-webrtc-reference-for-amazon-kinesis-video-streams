#ifndef ICE_CONTROLLER_H
#define ICE_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "ice_controller_data_types.h"

IceControllerResult_t IceController_Init( IceControllerContext_t *pCtx, SignalingControllerContext_t *pSignalingControllerContext );
IceControllerResult_t IceController_DeserializeIceCandidate( const char *pDecodeMessage, size_t decodeMessageLength, IceControllerCandidate_t *pCandidate );
IceControllerResult_t IceController_SetRemoteDescription( IceControllerContext_t *pCtx, const char *pRemoteClientId, size_t remoteClientIdLength, const char *pRemoteUserName, size_t remoteUserNameLength, const char *pRemotePassword, size_t remotePasswordLength );

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_H */
