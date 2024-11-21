#ifndef PEER_CONNECTION_SRTP_H
#define PEER_CONNECTION_SRTP_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

#include "peer_connection_data_types.h"

#define PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH      ( 1400 )

PeerConnectionResult_t PeerConnectionSrtp_Init( PeerConnectionSession_t * pSession );
PeerConnectionResult_t PeerConnectionSrtp_WriteH264Frame( PeerConnectionSession_t * pSession,
                                                          Transceiver_t * pTransceiver,
                                                          const PeerConnectionFrame_t * pFrame );
PeerConnectionResult_t PeerConnectionSrtp_WriteOpusFrame( PeerConnectionSession_t * pSession,
                                                          Transceiver_t * pTransceiver,
                                                          const PeerConnectionFrame_t * pFrame );
PeerConnectionResult_t PeerConnectionSrtp_HandleSrtpPacket( PeerConnectionSession_t * pSession,
                                                            uint8_t * pBuffer,
                                                            size_t bufferLength );
PeerConnectionResult_t PeerConnectionSrtp_HandleSrtcpPacket( PeerConnectionSession_t * pSession,
                                                             uint8_t * pBuffer,
                                                             size_t bufferLength );

#ifdef __cplusplus
}
#endif

#endif /* PEER_CONNECTION_SRTP_H */
