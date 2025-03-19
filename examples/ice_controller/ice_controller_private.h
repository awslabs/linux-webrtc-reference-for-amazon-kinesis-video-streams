#ifndef ICE_CONTROLLER_PRIVATE_H
#define ICE_CONTROLLER_PRIVATE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ice_controller_data_types.h"

#define ICE_CONTROLLER_IS_NAT_CONFIG_SET( pCtx, natConfig ) ( ( pCtx->natTraversalConfigBitmap & natConfig ) != 0 )

void IceController_UpdateState( IceControllerContext_t * pCtx,
                                IceControllerState_t newState );
void IceController_UpdateTimerInterval( IceControllerContext_t * pCtx,
                                        uint32_t newIntervalMs );
void IceController_CloseOtherCandidatePairs( IceControllerContext_t * pCtx,
                                             IceCandidatePair_t * pCandidatePair );
IceControllerResult_t IceControllerNet_ConvertIpString( const char * pIpAddr,
                                                        size_t ipAddrLength,
                                                        IceEndpoint_t * pDestinationIceEndpoint );
IceControllerResult_t IceControllerNet_Htons( uint16_t port,
                                              uint16_t * pOutPort );
void IceControllerNet_AddLocalCandidates( IceControllerContext_t * pCtx );
IceControllerResult_t IceControllerNet_HandleStunPacket( IceControllerContext_t * pCtx,
                                                         IceControllerSocketContext_t * pSocketContext,
                                                         uint8_t * pReceiveBuffer,
                                                         size_t receiveBufferLength,
                                                         IceEndpoint_t * pRemoteIceEndpoint,
                                                         IceCandidatePair_t * pCandidatePair );
IceControllerResult_t IceControllerNet_DnsLookUp( char * pUrl,
                                                  IceTransportAddress_t * pIceTransportAddress );
IceControllerResult_t IceControllerNet_SendPacket( IceControllerContext_t * pCtx,
                                                   IceControllerSocketContext_t * pSocketContext,
                                                   IceEndpoint_t * pRemoteEndpoint,
                                                   const uint8_t * pBuffer,
                                                   size_t bufferLength );
void IceControllerNet_FreeSocketContext( IceControllerContext_t * pCtx,
                                         IceControllerSocketContext_t * pSocketContext );
void IceControllerNet_LogStunPacket( uint8_t * pStunPacket,
                                     size_t stunPacketSize );
IceControllerResult_t IceController_SendTurnRefreshAllocation( IceControllerContext_t * pCtx,
                                                               IceCandidate_t * pTargetCandidate );
IceControllerResult_t IceController_SendTurnRefreshPermission( IceControllerContext_t * pCtx,
                                                               IceCandidatePair_t * pTargetCandidatePair );

IceControllerResult_t IceControllerSocketListener_Init( IceControllerContext_t * pCtx,
                                                        OnRecvNonStunPacketCallback_t onRecvNonStunPacketFunc,
                                                        void * pOnRecvNonStunPacketCallbackContext );
IceControllerResult_t IceControllerSocketListener_StartPolling( IceControllerContext_t * pCtx );
IceControllerResult_t IceControllerSocketListener_StopPolling( IceControllerContext_t * pCtx );

/* Debug utils. */
#if LIBRARY_LOG_LEVEL >= LOG_INFO
const char * IceControllerNet_LogIpAddressInfo( const IceEndpoint_t * pIceEndpoint,
                                                char * pIpBuffer,
                                                size_t ipBufferLength );
#endif /* #if LIBRARY_LOG_LEVEL >= LOG_VERBOSE  */

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_PRIVATE_H */
