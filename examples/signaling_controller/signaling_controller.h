#ifndef SIGNALING_CONTROLLER_H
#define SIGNALING_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signaling_controller_data_types.h"

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t *pCtx, SignalingControllerCredential_t *pCred, SignalingControllerReceiveMessageCallback receiveMessageCallback, void *pReceiveMessageCallbackContext );
void SignalingController_Deinit( SignalingControllerContext_t *pCtx );
SignalingControllerResult_t SignalingController_ConnectServers( SignalingControllerContext_t *pCtx );
SignalingControllerResult_t SignalingController_ProcessLoop( SignalingControllerContext_t *pCtx );
SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t *pCtx, SignalingControllerEventMessage_t *pEventMsg );

#ifdef __cplusplus
}
#endif

#endif /* SIGNALING_CONTROLLER_H */
