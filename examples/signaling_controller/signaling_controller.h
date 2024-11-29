#ifndef SIGNALING_CONTROLLER_H
#define SIGNALING_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signaling_controller_data_types.h"

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t * pCtx,
                                                      SignalingControllerCredential_t * pCred,
                                                      SignalingControllerReceiveMessageCallback receiveMessageCallback,
                                                      void * pReceiveMessageCallbackContext );
void SignalingController_Deinit( SignalingControllerContext_t * pCtx );
SignalingControllerResult_t SignalingController_IceServerReconnection( SignalingControllerContext_t * pCtx );
SignalingControllerResult_t SignalingController_ConnectServers( SignalingControllerContext_t * pCtx );
SignalingControllerResult_t SignalingController_ProcessLoop( SignalingControllerContext_t * pCtx );
SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t * pCtx,
                                                             SignalingControllerEventMessage_t * pEventMsg );
SignalingControllerResult_t SignalingController_QueryIceServerConfigs( SignalingControllerContext_t * pCtx,
                                                                       SignalingControllerIceServerConfig_t ** ppIceServerConfigs,
                                                                       size_t * pIceServerConfigsCount );
SignalingControllerResult_t SignalingController_GetSdpContentFromEventMsg( const char * pEventMessage,
                                                                           size_t eventMessageLength,
                                                                           uint8_t isSdpOffer,
                                                                           const char ** ppSdpMessage,
                                                                           size_t * pSdpMessageLength );
SignalingControllerResult_t SignalingController_DeserializeSdpContentNewline( const char * pSdpMessage,
                                                                              size_t sdpMessageLength,
                                                                              char * pFormalSdpMessage,
                                                                              size_t * pFormalSdpMessageLength );
SignalingControllerResult_t SignalingController_SerializeSdpContentNewline( const char * pSdpMessage,
                                                                            size_t sdpMessageLength,
                                                                            char * pEventSdpMessage,
                                                                            size_t * pEventSdpMessageLength );

#ifdef __cplusplus
}
#endif

#endif /* SIGNALING_CONTROLLER_H */
