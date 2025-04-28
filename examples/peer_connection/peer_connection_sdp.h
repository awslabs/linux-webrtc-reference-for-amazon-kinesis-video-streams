#ifndef PEER_CONNECTION_SDP_H
#define PEER_CONNECTION_SDP_H

#include <stdio.h>
#include <stdint.h>

#include "peer_connection_data_types.h"

PeerConnectionResult_t PeerConnectionSdp_DeserializeSdpMessage( PeerConnectionBufferSessionDescription_t * pBufferSessionDescription );
PeerConnectionResult_t PeerConnectionSdp_SetPayloadTypes( PeerConnectionSession_t * pSession,
                                                          PeerConnectionBufferSessionDescription_t * pRemoteBufferSessionDescription );
PeerConnectionResult_t PeerConnectionSdp_PopulateSessionDescription( PeerConnectionSession_t * pSession,
                                                                     PeerConnectionBufferSessionDescription_t * pRemoteBufferSessionDescription,
                                                                     PeerConnectionBufferSessionDescription_t * pLocalBufferSessionDescription,
                                                                     char * pOutputSerializedSdpMessage,
                                                                     size_t * pOutputSerializedSdpMessageLength );

#endif /* PEER_CONNECTION_SDP_H */
