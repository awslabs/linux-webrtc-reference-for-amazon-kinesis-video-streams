#ifndef ICE_CONTROLLER_PRIVATE_H
#define ICE_CONTROLLER_PRIVATE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ice_controller_data_types.h"

IceControllerResult_t IceControllerNet_ConvertIpString( const char *pIpAddr, size_t ipAddrLength, IceIPAddress_t *pDest );
IceControllerResult_t IceControllerNet_Htos( uint16_t port, uint16_t *pOutPort );

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_PRIVATE_H */
