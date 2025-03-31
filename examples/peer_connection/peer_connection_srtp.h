#ifndef PEER_CONNECTION_SRTP_H
#define PEER_CONNECTION_SRTP_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include <stdint.h>

#include "peer_connection_data_types.h"

#define PEER_CONNECTION_SRTP_RTP_PACKET_MAX_LENGTH      ( 1400 )

PeerConnectionResult_t PeerConnectionSrtp_Init( PeerConnectionSession_t * pSession );
PeerConnectionResult_t PeerConnectionSrtp_DeInit( PeerConnectionSession_t * pSession );
PeerConnectionResult_t PeerConnectionSrtp_HandleSrtpPacket( PeerConnectionSession_t * pSession,
                                                            uint8_t * pBuffer,
                                                            size_t bufferLength );
PeerConnectionResult_t PeerConnectionSrtp_ConstructSrtpPacket( PeerConnectionSession_t * pSession,
                                                               RtpPacket_t * pPacketRtp,
                                                               uint8_t * pOutputSrtpPacket,
                                                               size_t * pOutputSrtpPacketLength );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* PEER_CONNECTION_SRTP_H */
