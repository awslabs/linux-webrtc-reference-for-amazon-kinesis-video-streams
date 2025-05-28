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

#ifndef PEER_CONNECTION_H265_HELPER_H
#define PEER_CONNECTION_H265_HELPER_H

/* Standard includes. */
#include <stdint.h>

#include "peer_connection_data_types.h"

PeerConnectionResult_t GetH265PacketProperty( PeerConnectionJitterBufferPacket_t * pPacket,
                                              uint8_t * pIsStartPacket );

PeerConnectionResult_t FillFrameH265( PeerConnectionJitterBuffer_t * pJitterBuffer,
                                      uint16_t rtpSeqStart,
                                      uint16_t rtpSeqEnd,
                                      uint8_t * pOutBuffer,
                                      size_t * pOutBufferLength,
                                      uint32_t * pRtpTimestamp );

PeerConnectionResult_t PeerConnectionSrtp_WriteH265Frame( PeerConnectionSession_t * pSession,
                                                          Transceiver_t * pTransceiver,
                                                          const PeerConnectionFrame_t * pFrame );

#endif /* PEER_CONNECTION_H265_HELPER_H */
