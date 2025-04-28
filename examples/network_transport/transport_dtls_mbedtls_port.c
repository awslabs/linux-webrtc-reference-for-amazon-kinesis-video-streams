#include "transport_dtls_mbedtls.h"
#include "transport_dtls_mbedtls_port.h"

/* void mbedtls_timing_set_delay( void * data, */
/*                                uint32_t int_ms, */
/*                                uint32_t fin_ms ) { */
/*     // LogDebug(("mbedtls_timing_set_delay: int_ms %li,fin_ms %li", int_ms, fin_ms)); ; */
/*     DtlsSessionTimer_t * ctx = ( DtlsSessionTimer_t * ) data; */
/*     ctx->start_ticks = xTaskGetTickCount(); */

/*     ctx->int_ms = ctx->start_ticks + int_ms; */
/*     ctx->fin_ms = ctx->start_ticks + fin_ms; */

/*     // LogDebug(("mbedtls_timing_set_delay start_ticks: %lli",ctx->start_ticks)); */
/* } */

/* int mbedtls_timing_get_delay( void * data ) { */
/*     // LogDebug(("mbedtls_timing_get_delay")); */
/*     DtlsSessionTimer_t * ctx = ( DtlsSessionTimer_t * ) data; */
/*     int64_t elapsed_ticks = xTaskGetTickCount() - ctx->start_ticks; */
/*     int64_t elapsed_ms = elapsed_ticks / * * portTICK_PERIOD_MS * /; */
/*     // LogDebug(("mbedtls_timing_get_delay elapsed_ticks: %lli",elapsed_ticks)); */
/*     if( ctx->fin_ms == 0 ) */
/*     { */
/*         return -1; */
/*     } */

/*     if( elapsed_ms >= ctx->fin_ms ) */
/*     { */
/*         return 2; */
/*     } */

/*     if( elapsed_ms >= ctx->int_ms ) */
/*     { */
/*         return 1; */
/*     } */

/*     return 0; */
/* } */
