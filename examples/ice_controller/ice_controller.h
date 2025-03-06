#ifndef ICE_CONTROLLER_H
#define ICE_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "ice_controller_data_types.h"

IceControllerResult_t IceController_Init( IceControllerContext_t * pCtx,
                                          IceControllerInitConfig_t * pInitConfig );
IceControllerResult_t IceController_Destroy( IceControllerContext_t * pCtx );
IceControllerResult_t IceController_AddressClosing( IceControllerContext_t * pCtx );
IceControllerResult_t IceController_DeserializeIceCandidate( const char * pDecodeMessage,
                                                             size_t decodeMessageLength,
                                                             IceControllerCandidate_t * pCandidate );
IceControllerResult_t IceController_Start( IceControllerContext_t * pCtx,
                                           const char * pLocalUserName,
                                           size_t localUserNameLength,
                                           const char * pLocalPassword,
                                           size_t localPasswordLength,
                                           const char * pRemoteUserName,
                                           size_t remoteUserNameLength,
                                           const char * pRemotePassword,
                                           size_t remotePasswordLength,
                                           const char * pCombinedName,
                                           size_t combinedNameLength );
IceControllerResult_t IceController_ProcessLoop( IceControllerContext_t * pCtx );
IceControllerResult_t IceController_AddRemoteCandidate( IceControllerContext_t * pCtx,
                                                        IceRemoteCandidateInfo_t * pRemoteCandidate );
IceControllerResult_t IceController_ProcessIceCandidatesAndPairs( IceControllerContext_t * pCtx );
IceControllerResult_t IceController_SendToRemotePeer( IceControllerContext_t * pCtx,
                                                      const uint8_t * pBuffer,
                                                      size_t bufferLength );
IceControllerResult_t IceController_AddIceServerConfig( IceControllerContext_t * pCtx,
                                                        IceControllerIceServerConfig_t * pIceServersConfig );
IceControllerResult_t IceController_PeriodConnectionCheck( IceControllerContext_t * pCtx );

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_H */
