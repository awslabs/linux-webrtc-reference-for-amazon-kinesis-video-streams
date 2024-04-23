#ifndef ICE_CONTROLLER_H
#define ICE_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define KVS_IP_ADDRESS_STRING_BUFFER_LEN                        ( 46 )


#define HUNDREDS_OF_NANOS_IN_A_MICROSECOND                      ( 10LL )
#define HUNDREDS_OF_NANOS_IN_A_MILLISECOND                      ( HUNDREDS_OF_NANOS_IN_A_MICROSECOND * 1000LL )
#define HUNDREDS_OF_NANOS_IN_A_SECOND                           ( HUNDREDS_OF_NANOS_IN_A_MILLISECOND * 1000LL )
#define CONNECTION_LISTENER_SOCKET_WAIT_FOR_DATA_TIMEOUT        ( 200 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND )

void getLocalIPAdresses( void );

int IceController_Init( char * localName, char * remoteName, char * localPwd, char * remotePwd );

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_H */
