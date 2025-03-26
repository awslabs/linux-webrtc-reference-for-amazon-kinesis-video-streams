#ifndef PEER_CONNECTION_SCTP_H
#define PEER_CONNECTION_SCTP_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "sctp_utils.h"
#include "peer_connection.h"

#include "peer_connection_data_types.h"

#ifndef DATACHANNEL_CUSTOM_CALLBACK_HOOK
#define DATACHANNEL_CUSTOM_CALLBACK_HOOK  ( 1U )
#endif


#define MASTER_DATA_CHANNEL_MESSAGE "This message is from the FreeRTOS-WebRTC-Application KVS Master"

PeerConnectionDataChannel_t * PeerConnectionSCTP_AllocateDataChannel( void );

PeerConnectionResult_t PeerConnectionSCTP_DeallocateDataChannel( PeerConnectionDataChannel_t * pChannel );

PeerConnectionResult_t PeerConnectionSCTP_CreateDataChannel( PeerConnectionSession_t * pSession,
                                                             char * pcDataChannelName,
                                                             SctpDataChannelInitInfo_t * pDataChannelInitInfo,
                                                             PeerConnectionDataChannel_t ** ppChannel );

PeerConnectionResult_t PeerConnectionSCTP_CloseDataChannel( PeerConnectionDataChannel_t * pChannel );

PeerConnectionResult_t PeerConnectionSCTP_DataChannelSend( PeerConnectionDataChannel_t * pChannel,
                                                           uint8_t isBinary,
                                                           uint8_t * pMessage,
                                                           uint32_t pMessageLen );

PeerConnectionResult_t PeerConnectionSCTP_AllocateSCTP( PeerConnectionSession_t * pSession );

PeerConnectionResult_t PeerConnectionSCTP_DeallocateSCTP( PeerConnectionSession_t * pSession );

void PeerConnectionSCTP_ProcessSCTPData( PeerConnectionSession_t * pSession,
                                         uint8_t * receiveBuffer,
                                         int readBytes );

#if ( DATACHANNEL_CUSTOM_CALLBACK_HOOK != 0 )
OnDataChannelMessageReceived_t PeerConnectionSCTP_SetChannelOnMessageCallbackHook( PeerConnectionSession_t * pPeerConnectionSession,
                                                                                    uint32_t ulChannelId,
                                                                                    const uint8_t * pucName,
                                                                                    uint32_t ulNameLen );
#endif

#ifdef __cplusplus
}
#endif
#endif /* PEER_CONNECTION_SCTP_H */
