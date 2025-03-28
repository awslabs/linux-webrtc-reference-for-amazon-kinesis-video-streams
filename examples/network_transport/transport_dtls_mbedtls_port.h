#ifndef TRANSPORT_DTLS_PORT_H
#define TRANSPORT_DTLS_PORT_H

#pragma once

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdint.h>

// include for retransmission timer
//#include "timers.h"

// need for x509 cert generation
#include "time.h"

void mbedtls_timing_set_delay( void * data,
                               uint32_t int_ms,
                               uint32_t fin_ms );

int mbedtls_timing_get_delay( void * data );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* TRANSPORT_DTLS_PORT_H */
