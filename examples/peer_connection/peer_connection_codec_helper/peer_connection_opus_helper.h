#ifndef PEER_CONNECTION_OPUS_HELPER_H
#define PEER_CONNECTION_OPUS_HELPER_H

/* Standard includes. */
#include <stdint.h>

#include "peer_connection_data_types.h"

PeerConnectionResult_t GetOpusPacketProperty(
    PeerConnectionJitterBufferPacket_t * pPacket, uint8_t * pIsStartPacket );

PeerConnectionResult_t FillFrameOpus(
    PeerConnectionJitterBuffer_t * pJitterBuffer, uint16_t rtpSeqStart,
    uint16_t rtpSeqEnd, uint8_t * pOutBuffer, size_t * pOutBufferLength,
    uint32_t * pRtpTimestamp );

PeerConnectionResult_t PeerConnectionSrtp_WriteOpusFrame(
    PeerConnectionSession_t * pSession, Transceiver_t * pTransceiver,
    const PeerConnectionFrame_t * pFrame );

#endif /* PEER_CONNECTION_OPUS_HELPER_H */
