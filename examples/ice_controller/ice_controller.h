/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ICE_CONTROLLER_H
#define ICE_CONTROLLER_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include "ice_controller_data_types.h"
#include <stdio.h>

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
IceControllerResult_t IceController_AddRemoteCandidate( IceControllerContext_t * pCtx,
    IceRemoteCandidateInfo_t * pRemoteCandidate );
IceControllerResult_t IceController_ProcessIceCandidatesAndPairs( IceControllerContext_t * pCtx );
IceControllerResult_t IceController_SendToRemotePeer( IceControllerContext_t * pCtx,
    const uint8_t * pBuffer,
    size_t bufferLength );
IceControllerResult_t IceController_AddIceServerConfig( IceControllerContext_t * pCtx,
    IceControllerIceServerConfig_t * pIceServersConfig );
IceControllerResult_t IceController_PeriodConnectionCheck( IceControllerContext_t * pCtx );
void IceController_HandleEvent( IceControllerContext_t * pCtx,
    IceControllerEvent_t event );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* ICE_CONTROLLER_H */
