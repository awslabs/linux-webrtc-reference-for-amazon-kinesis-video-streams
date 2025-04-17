#ifndef SDP_CONTROLLER_H
#define SDP_CONTROLLER_H

#include <stdio.h>
#include <stdint.h>
#include "sdp_controller_data_types.h"
#include "peer_connection_data_types.h"

#define SDP_CONTROLLER_ORIGIN_DEFAULT_SESSION_NAME "-"

#define SDP_CONTROLLER_MESSAGE_TEMPLATE_HEAD "{\"type\": \"%s\", \"sdp\": \""
#define SDP_CONTROLLER_MESSAGE_TEMPLATE_TAIL "\"}"

SdpControllerResult_t SdpController_DeserializeSdpOffer( const char * pSdpOfferContent,
                                                         size_t sdpOfferContentLength,
                                                         SdpControllerSdpDescription_t * pOffer );
SdpControllerResult_t SdpController_SerializeSdpMessageByDescription( SdpControllerMessageType_t messageType,
                                                                      SdpControllerSdpDescription_t * pSdpDescription,
                                                                      char * pOutputSerializedSdpMessage,
                                                                      size_t * pOutputSerializedSdpMessageLength );
SdpControllerResult_t SdpController_PopulateSingleMedia( SdpControllerMediaDescription_t * pRemoteMediaDescription,
                                                         SdpControllerPopulateMediaConfiguration_t populateConfiguration,
                                                         SdpControllerMediaDescription_t * pLocalMediaDescription,
                                                         uint32_t currentMediaIdx,
                                                         char ** ppBuffer,
                                                         size_t * pBufferLength,
                                                         TransceiverTrackKind_t trackKind );
SdpControllerResult_t SdpController_PopulateSessionDescription( SdpControllerSdpDescription_t * pRemoteSessionDescription,
                                                                SdpControllerPopulateSessionConfiguration_t populateConfiguration,
                                                                SdpControllerSdpDescription_t * pLocalSessionDescription,
                                                                char ** ppBuffer,
                                                                size_t * pBufferLength );

#endif /* SDP_CONTROLLER_H */
