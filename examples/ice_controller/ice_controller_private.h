#ifndef ICE_CONTROLLER_PRIVATE_H
#define ICE_CONTROLLER_PRIVATE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ice_controller_data_types.h"

#define ICE_CONTROLLER_CANDIDATE_TYPE_HOST_STRING "host"
#define ICE_CONTROLLER_CANDIDATE_TYPE_SRFLX_STRING "srflx"
#define ICE_CONTROLLER_CANDIDATE_TYPE_PRFLX_STRING "prflx"
#define ICE_CONTROLLER_CANDIDATE_TYPE_RELAY_STRING "relay"
#define ICE_CONTROLLER_CANDIDATE_TYPE_UNKNOWN_STRING "unknown"
#define ICE_CANDIDATE_JSON_TEMPLATE "{\"candidate\":\"%.*s\",\"sdpMid\":\"0\",\"sdpMLineIndex\":0}"
#define ICE_CANDIDATE_JSON_MAX_LENGTH ( 1024 )
#define ICE_CANDIDATE_JSON_CANDIDATE_IPV4_TEMPLATE "candidate:%lu 1 udp %u %d.%d.%d.%d %d typ %s raddr 0.0.0.0 rport 0 generation 0 network-cost 999"
#define ICE_CANDIDATE_JSON_CANDIDATE_IPV6_TEMPLATE "candidate:%lu 1 udp %u %02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X "\
                                                   "%d typ %s raddr ::/0 rport 0 generation 0 network-cost 999"
#define ICE_CANDIDATE_JSON_CANDIDATE_MAX_LENGTH ( 512 )

IceControllerResult_t IceControllerNet_InitRemoteInfo( IceControllerRemoteInfo_t *pRemoteInfo );
IceControllerResult_t IceControllerNet_ConvertIpString( const char *pIpAddr, size_t ipAddrLength, IceEndpoint_t *pDest );
IceControllerResult_t IceControllerNet_Htons( uint16_t port, uint16_t *pOutPort );
IceControllerResult_t IceControllerNet_AddLocalCandidates( IceControllerContext_t *pCtx, IceControllerRemoteInfo_t *pRemoteInfo );
IceControllerResult_t IceControllerNet_AttachPolling( IceControllerContext_t *pCtx, IceControllerSocketContext_t *pSocketContext );
IceControllerResult_t IceControllerNet_HandleRxPacket( IceControllerContext_t *pCtx, IceControllerSocketContext_t *pSocketContext );
IceControllerResult_t IceControllerNet_DnsLookUp( char *pUrl, StunAttributeAddress_t *pIpAddress );
IceControllerResult_t IceControllerNet_SendPacket( IceControllerSocketContext_t *pSocketContext, IceEndpoint_t *pDestinationIpAddress, char *pBuffer, size_t length );
const char *IceControllerNet_LogIpAddressInfo( const IceEndpoint_t *pIceIpAddress, char *pIpBuffer, size_t ipBufferLength );
void IceControllerNet_LogStunPacket( uint8_t *pStunPacket, size_t stunPacketSize );
void IceController_PrintMetrics( IceControllerContext_t * pCtx );

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_PRIVATE_H */
