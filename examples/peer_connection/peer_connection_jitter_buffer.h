#ifndef PEER_CONNECTION_JITTER_BUFFER_H
#define PEER_CONNECTION_JITTER_BUFFER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Standard includes. */
#include <stdint.h>

/* FreeRTOS includes. */
//#include "FreeRTOS.h"

#include "peer_connection_data_types.h"

PeerConnectionResult_t PeerConnectionJitterBuffer_Create( PeerConnectionJitterBuffer_t * pJitterBuffer,
                                                          OnJitterBufferFrameReadyCallback_t onFrameReadyCallbackFunc,
                                                          void * pOnFrameReadyCallbackContext,
                                                          OnJitterBufferFrameDropCallback_t onFrameDropCallbackFunc,
                                                          void * pOnFrameDropCallbackContext,
                                                          uint32_t tolerenceBufferSec,  // buffer time in seconds
                                                          uint32_t codec,
                                                          uint32_t clockRate );

void PeerConnectionJitterBuffer_Free( PeerConnectionJitterBuffer_t * pJitterBuffer );

PeerConnectionResult_t PeerConnectionJitterBuffer_AllocateBuffer( PeerConnectionJitterBuffer_t * pJitterBuffer,
                                                                  PeerConnectionJitterBufferPacket_t ** ppOutPacket,
                                                                  size_t packetBufferSize,
                                                                  uint16_t rtpSeq );

PeerConnectionResult_t PeerConnectionJitterBuffer_GetPacket( PeerConnectionJitterBuffer_t * pJitterBuffer,
                                                             uint16_t rtpSeq,
                                                             PeerConnectionJitterBufferPacket_t ** ppOutPacket );

PeerConnectionResult_t PeerConnectionJitterBuffer_Push( PeerConnectionJitterBuffer_t * pJitterBuffer,
                                                        PeerConnectionJitterBufferPacket_t * pPacket );

PeerConnectionResult_t PeerConnectionJitterBuffer_FillFrame( PeerConnectionJitterBuffer_t * pJitterBuffer,
                                                             uint16_t rtpSeqStart,
                                                             uint16_t rtpSeqEnd,
                                                             uint8_t * pOutBuffer,
                                                             size_t * pOutBufferLength,
                                                             uint32_t * pRtpTimestamp );

#ifdef __cplusplus
}
#endif

#endif /* PEER_CONNECTION_JITTER_BUFFER_H */
