#ifndef PEER_CONNECTION_H
#define PEER_CONNECTION_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdio.h>
#include <stdint.h>

#include "peer_connection_data_types.h"
#include "sdp_controller_data_types.h"

    PeerConnectionResult_t PeerConnection_Init( PeerConnectionSession_t * pSession,
                                                PeerConnectionSessionConfiguration_t * pSessionConfig );
    PeerConnectionResult_t PeerConnection_Start( PeerConnectionSession_t * pSession );
    PeerConnectionResult_t PeerConnection_AddTransceiver( PeerConnectionSession_t * pSession,
                                                          Transceiver_t * pTransceiver );
#if ENABLE_SCTP_DATA_CHANNEL
/* Enable data channel to be included in the SDP offer. */
        PeerConnectionResult_t PeerConnection_AddDataChannel( PeerConnectionSession_t * pSession );
#endif /* ENABLE_SCTP_DATA_CHANNEL */

#if ENABLE_TWCC_SUPPORT
/* Enable TWCC Bandwidth Estimation Callback to change the media bitrates along side tith the Handler Callback */
        PeerConnectionResult_t PeerConnection_SetSenderBandwidthEstimationCallback( PeerConnectionSession_t * pSession,
                                                                                    OnBandwidthEstimationCallback_t onBandwidthEstimationCallback,
                                                                                    void * pUserContext );
#endif /* ENABLE_TWCC_SUPPORT */

    PeerConnectionResult_t PeerConnection_MatchTransceiverBySsrc( PeerConnectionSession_t * pSession,
                                                                  uint32_t ssrc,
                                                                  const Transceiver_t ** ppTransceiver );
    PeerConnectionResult_t PeerConnection_SetLocalDescription( PeerConnectionSession_t * pSession,
                                                               const PeerConnectionBufferSessionDescription_t * pBufferSessionDescription );
    PeerConnectionResult_t PeerConnection_SetRemoteDescription( PeerConnectionSession_t * pSession,
                                                                const PeerConnectionBufferSessionDescription_t * pBufferSessionDescription );
    PeerConnectionResult_t PeerConnection_SetVideoOnFrame( PeerConnectionSession_t * pSession,
                                                           OnFrameReadyCallback_t onFrameReadyCallbackFunc,
                                                           void * pOnFrameReadyCallbackCustomContext );
    PeerConnectionResult_t PeerConnection_SetAudioOnFrame( PeerConnectionSession_t * pSession,
                                                           OnFrameReadyCallback_t onFrameReadyCallbackFunc,
                                                           void * pOnFrameReadyCallbackCustomContext );
    PeerConnectionResult_t PeerConnection_AddRemoteCandidate( PeerConnectionSession_t * pSession,
                                                              const char * pDecodeMessage,
                                                              size_t decodeMessageLength );
    PeerConnectionResult_t PeerConnection_CloseSession( PeerConnectionSession_t * pSession );
    PeerConnectionResult_t PeerConnection_WriteFrame( PeerConnectionSession_t * pSession,
                                                      Transceiver_t * pTransceiver,
                                                      const PeerConnectionFrame_t * pFrame );
    PeerConnectionResult_t PeerConnection_CreateAnswer( PeerConnectionSession_t * pSession,
                                                        PeerConnectionBufferSessionDescription_t * pOutputBufferSessionDescription,
                                                        char * pOutputSerializedSdpMessage,
                                                        size_t * pOutputSerializedSdpMessageLength );
    PeerConnectionResult_t PeerConnection_CreateOffer( PeerConnectionSession_t * pSession,
                                                       PeerConnectionBufferSessionDescription_t * pOutputBufferSessionDescription,
                                                       char * pOutputSerializedSdpMessage,
                                                       size_t * pOutputSerializedSdpMessageLength );
    PeerConnectionResult_t PeerConnection_SetOnLocalCandidateReady( PeerConnectionSession_t * pSession,
                                                                    OnIceCandidateReadyCallback_t onLocalCandidateReadyCallbackFunc,
                                                                    void * pOnLocalCandidateReadyCallbackCustomContext );
    PeerConnectionResult_t PeerConnection_AddIceServerConfig( PeerConnectionSession_t * pSession,
                                                              PeerConnectionSessionConfiguration_t * pSessionConfig );
    PeerConnectionResult_t PeerConnection_SetPictureLossIndicationCallback( PeerConnectionSession_t * pSession,
                                                                            OnPictureLossIndicationCallback_t onPictureLossIndicationCallback,
                                                                            void * pUserContext );

#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* PEER_CONNECTION_H */
