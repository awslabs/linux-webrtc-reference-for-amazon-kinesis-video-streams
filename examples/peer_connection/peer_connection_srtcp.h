#ifndef PEER_CONNECTION_SRTCP_H
#define PEER_CONNECTION_SRTCP_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include <stdint.h>

#include "peer_connection_data_types.h"

/* 28 Bytes of RTCP with 0 Reception Reports + 14 bytes of SRTCP */
#define PEER_CONNECTION_SRTCP_RTCP_PACKET_MIN_LENGTH      ( 42 )

    PeerConnectionResult_t PeerConnectionSrtp_HandleSrtcpPacket( PeerConnectionSession_t * pSession,
                                                                 uint8_t * pBuffer,
                                                                 size_t bufferLength );
    PeerConnectionResult_t PeerConnectionSrtcp_ConstructSenderReportPacket( PeerConnectionSession_t * pSession,
                                                                            RtcpSenderReport_t * pSenderReport,
                                                                            uint8_t * pOutputSrtcpPacket,
                                                                            size_t * pOutputSrtcpPacketLength );

#ifdef __cplusplus
        }
#endif

#endif /* PEER_CONNECTION_SRTCP_H */
