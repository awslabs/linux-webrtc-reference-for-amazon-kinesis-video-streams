#ifndef MBEDTLS_CUSTOM_CONFIG_H
#define MBEDTLS_CUSTOM_CONFIG_H

#define MBEDTLS_SSL_DTLS_SRTP

#include "mbedtls/config.h"

#if DEBUG != 0
    #define MBEDTLS_DEBUG_C
#else /* DEBUG != 0 */
    #undef MBEDTLS_DEBUG_C
#endif /* DEBUG != 0 */

#endif /* MBEDTLS_CUSTOM_CONFIG_H */
