#ifndef TRANSPORT_DTLS_PORT_H
#define TRANSPORT_DTLS_PORT_H

#include <stdint.h>

/* include for retransmission timer */
/*#include "timers.h" */

/* need for x509 cert generation */
#include "time.h"

void mbedtls_timing_set_delay( void * data,
                               uint32_t int_ms,
                               uint32_t fin_ms );

int mbedtls_timing_get_delay( void * data );

#endif /* TRANSPORT_DTLS_PORT_H */
