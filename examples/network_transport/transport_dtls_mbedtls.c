/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/**
 * @file transport_dtls_mbedtls.c
 * @brief DTLS transport interface implementations. This implementation uses
 * mbedTLS.
 */

#include "logging.h"

/* Standard includes. */
#include <string.h>
#include <assert.h>

#include "mbedtls/config.h"
#include "mbedtls/pem.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/version.h"
#include "mbedtls/debug.h"

#ifdef MBEDTLS_PSA_CRYPTO_C
/* MbedTLS PSA Includes */
#include "psa/crypto.h"
#include "psa/crypto_values.h"
#endif /* MBEDTLS_PSA_CRYPTO_C */

#ifdef MBEDTLS_DTLS_DEBUG_C
#include "mbedtls/debug.h"
#endif /* MBEDTLS_DTLS_DEBUG_C */

/* MBedTLS Bio UDP sockets wrapper include. */
#include "mbedtls_bio_udp_sockets_wrapper.h"

/* DTLS transport header. */
#include "transport_dtls_mbedtls.h"

/* OS specific port header. */
#include "transport_dtls_mbedtls_port.h"

/*-----------------------------------------------------------*/

/**  https://tools.ietf.org/html/rfc5764#section-4.1.2 */
mbedtls_ssl_srtp_profile DTLS_SRTP_SUPPORTED_PROFILES[] = {
#if ( MBEDTLS_VERSION_NUMBER == 0x03000000 || MBEDTLS_VERSION_NUMBER == 0x03020100 )
    MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80,
    MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32,
    MBEDTLS_TLS_SRTP_UNSET,
#else
    MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80,
    MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32,
    MBEDTLS_TLS_SRTP_UNSET
#endif
};

#define DTLS_MTU_MAX_SIZE ( 1500 )

/**
 * @brief Utility for converting the high-level code in an mbedTLS error to
 * string, if the code-contains a high-level code; otherwise, using a default
 * string.
 */
#define mbedtlsHighLevelCodeOrDefault( mbedTlsCode ) "mbedTLS high level Error"

/**
 * @brief Utility for converting the level-level code in an mbedTLS error to
 * string, if the code-contains a level-level code; otherwise, using a default
 * string.
 */
#define mbedtlsLowLevelCodeOrDefault( mbedTlsCode ) "mbedTLS low level Error"

/*-----------------------------------------------------------*/

/**
 * @brief Initialize the mbed DTLS structures in a network connection.
 *
 * @param[in] pSslContext The DTLS SSL context to initialize.
 */
static void DtlsSslContextInit( DtlsSSLContext_t * pSslContext );

/**
 * @brief Free the mbed DTLS structures in a network connection.
 *
 * @param[in] pSslContext The SSL context to free.
 */
static void DtlsSslContextFree( DtlsSSLContext_t * pSslContext );

/**
 * @brief Passes DTLS credentials to the OpenSSL library.
 *
 * Provides the root CA certificate, client certificate, and private key to the
 * OpenSSL library. If the client certificate or private key is not NULL, mutual
 * authentication is used when performing the DTLS handshake.
 *
 * @param[out] pSslContext SSL context to which the credentials are to be
 * imported.
 * @param[in] pNetworkCredentials DTLS credentials to be imported.
 *
 * @return 0 on success; otherwise, failure;
 */
static int32_t setCredentials( DtlsSSLContext_t * pSslContext,
                               DtlsNetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Setup DTLS by initializing contexts and setting configurations.
 *
 * @param[in] pDtlsNetworkContext Network context.
 * @param[in] pHostName Remote host name, used for server name indication.
 * @param[in] pNetworkCredentials DTLS setup parameters.
 *
 * @return #DTLS_TRANSPORT_SUCCESS, #DTLS_TRANSPORT_INSUFFICIENT_MEMORY,
 * #DTLS_TRANSPORT_INVALID_CREDENTIALS, or #DTLS_TRANSPORT_INTERNAL_ERROR.
 */
static DtlsTransportStatus_t dtlsSetup( DtlsNetworkContext_t * pDtlsNetworkContext,
                                        DtlsNetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Perform the DTLS handshake on a UDP connection.
 *
 * @param[in] pDtlsNetworkContext Network context.
 * @param[in] pNetworkCredentials DTLS setup parameters.
 *
 * @return #DTLS_TRANSPORT_SUCCESS, #DTLS_TRANSPORT_HANDSHAKE_FAILED, or
 * #DTLS_TRANSPORT_INTERNAL_ERROR.
 */
static DtlsTransportStatus_t dtlsHandshake( DtlsNetworkContext_t * pNetworkContext,
                                            const DtlsNetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Initialize mbedTLS.
 *
 * @param[out] entropyContext mbed DTLS entropy context for generation of random
 * numbers.
 * @param[out] ctrDrbgContext mbed DTLS CTR DRBG context for generation of
 * random numbers.
 *
 * @return #DTLS_TRANSPORT_SUCCESS, or #DTLS_TRANSPORT_INTERNAL_ERROR.
 */
static DtlsTransportStatus_t initMbedtls( mbedtls_entropy_context * pEntropyContext,
                                          mbedtls_ctr_drbg_context * pCtrDrbgContext );

/*-----------------------------------------------------------*/

#ifdef MBEDTLS_DEBUG_C
void dtls_mbedtls_string_printf( void * sslContext,
                                 int level,
                                 const char * file,
                                 int line,
                                 const char * str )
{
    if( ( str != NULL ) && ( file != NULL ) )
    {
        LogDebug( ( "%s:%d: [%d] %s", file, line, level, str ) );
    }
}
#endif /* MBEDTLS_DEBUG_C */

/*-----------------------------------------------------------*/

static int DtlsUdpSendWrap( void * pCustomCtx,
                                      const unsigned char * pBuf,
                                      size_t len )
{
    int ret = 0;
    DtlsTransportParams_t * pDtlsTransportParams = (DtlsTransportParams_t*) pCustomCtx;
    static char sendingBuffer[ DTLS_MTU_MAX_SIZE ];
    size_t sendingBufferSize = DTLS_MTU_MAX_SIZE;
    const uint8_t * pBuffer;
    size_t * pBufferLength;
    size_t diff = 0;

    if( pDtlsTransportParams == NULL )
    {
        LogError( ("Invalid parameter: pCustomCtx is NULL.") );
        return -1;
    }

    if( ret == 0 )
    {
        if( pDtlsTransportParams->onDtlsSendHook != NULL )
        {
            pDtlsTransportParams->onDtlsSendHook( pDtlsTransportParams->pOnDtlsSendCustomContext, pBuf, len, sendingBuffer, &sendingBufferSize );
            diff = sendingBufferSize - len;
            pBuffer = sendingBuffer;
            pBufferLength = &sendingBufferSize;
        }
        else
        {
            pBuffer = pBuf;
            pBufferLength = &len;
        }
    }

    if( ret == 0 )
    {
        ret = xMbedTLSBioUDPSocketsWrapperSend( pDtlsTransportParams->udpSocket,
                                      pBuffer,
                                      *pBufferLength );
        if( ret > diff )
        {
            ret -= diff;
        }
    }

    return ret;
}
/*-----------------------------------------------------------*/

static int DtlsUdpRecvWrap( void * pCustomCtx,
                                      unsigned char * pBuf,
                                      size_t len )
{
    int ret = 0;
    DtlsTransportParams_t * pDtlsTransportParams = (DtlsTransportParams_t*) pCustomCtx;
    int receivedLength;

    if( pDtlsTransportParams == NULL )
    {
        LogError( ("Invalid parameter: pCustomCtx is NULL.") );
        return -1;
    }

    if( ret == 0 )
    {
        ret = xMbedTLSBioUDPSocketsWrapperRecv( pDtlsTransportParams->udpSocket,
                                      pBuf,
                                      len );
    }

    if( ret > 0 )
    {
        if( pDtlsTransportParams->onDtlsRecvHook != NULL )
        {
            receivedLength = ret;
            ret = pDtlsTransportParams->onDtlsRecvHook( pDtlsTransportParams->pOnDtlsRecvCustomContext, pBuf, receivedLength, pBuf, &len );
            if( ret == 0 && len == 0 )
            {
                /* Receiving data for other candidate, drop it and keep receiving. */
                ret = MBEDTLS_ERR_SSL_WANT_READ;
            }
            else if( ret == 0 )
            {
                ret = len;
            }
            else
            {
                /* Empty else marker. */
            }
        }
    }

    return ret;
}
/*-----------------------------------------------------------*/

static void DtlsSslContextInit( DtlsSSLContext_t * pSslContext )
{
    assert( pSslContext != NULL );

    mbedtls_ssl_config_init( &( pSslContext->config ) );
    mbedtls_ssl_init( &( pSslContext->context ) );
#ifdef MBEDTLS_DEBUG_C
    mbedtls_debug_set_threshold( 1 );
    mbedtls_ssl_conf_dbg( &( pSslContext->config ),
                          dtls_mbedtls_string_printf,
                          NULL );
#endif /* MBEDTLS_DEBUG_C */
}
/*-----------------------------------------------------------*/

static void DtlsSslContextFree( DtlsSSLContext_t * pSslContext )
{
    assert( pSslContext != NULL );

    mbedtls_ssl_config_free( &( pSslContext->config ) );
    mbedtls_ssl_free( &( pSslContext->context ) );
    mbedtls_entropy_free( &( pSslContext->entropyContext ) );
    mbedtls_ctr_drbg_free( &( pSslContext->ctrDrbgContext ) );
}
/*-----------------------------------------------------------*/

int dtlsSessionKeyDerivationCallback( void * customData,
                                      const unsigned char * pMasterSecret,
                                      const unsigned char * pKeyBlock,
                                      size_t maclen,
                                      size_t keylen,
                                      size_t ivlen,
                                      const unsigned char clientRandom[MAX_DTLS_RANDOM_BYTES_LEN],
                                      const unsigned char serverRandom[MAX_DTLS_RANDOM_BYTES_LEN],
                                      mbedtls_tls_prf_types tlsProfile )
{
    ( ( void ) pKeyBlock );
    ( ( void ) maclen );
    ( ( void  )keylen );
    ( ( void ) ivlen );
    DtlsSSLContext_t * pSslContext = ( DtlsSSLContext_t * )customData;
    TlsKeys * pKeys = ( TlsKeys * ) &pSslContext->tlsKeys;

    memcpy( pKeys->masterSecret,
            pMasterSecret,
            sizeof( pKeys->masterSecret ) );
    memcpy( pKeys->randBytes,
            clientRandom,
            MAX_DTLS_RANDOM_BYTES_LEN );
    memcpy( pKeys->randBytes + MAX_DTLS_RANDOM_BYTES_LEN,
            serverRandom,
            MAX_DTLS_RANDOM_BYTES_LEN );
    pKeys->tlsProfile = tlsProfile;

    return 0;
}

/*-----------------------------------------------------------*/
static int32_t setCredentials( DtlsSSLContext_t * pSslContext,
                               DtlsNetworkCredentials_t * pNetworkCredentials )
{
    LogDebug( ( "setCredentials" ) );
    int32_t mbedtlsError = 0;

    assert( pSslContext != NULL );
    assert( pNetworkCredentials != NULL );

    /* Set up the certificate security profile, starting from the default value.
     */
    pSslContext->certProfile = mbedtls_x509_crt_profile_default;

    /* Set SSL authmode and the RNG context. */
    mbedtls_ssl_conf_authmode( &( pSslContext->config ),
                               MBEDTLS_SSL_VERIFY_OPTIONAL );
    LogDebug( ( "before mbedtls_ssl_conf_rng" ) );
    mbedtls_ssl_conf_rng( &( pSslContext->config ),
                          mbedtls_ctr_drbg_random,
                          &( pSslContext->ctrDrbgContext ) );
    LogDebug( ( "before mbedtls_ssl_conf_cert_profile" ) );
    mbedtls_ssl_conf_cert_profile( &( pSslContext->config ),
                                   &( pSslContext->certProfile ) );

    if( pNetworkCredentials->pClientCert != NULL )
    {
        if( pNetworkCredentials->pPrivateKey != NULL )
        {
            if( mbedtlsError == 0 )
            {
                LogInfo( ( "Before mbedtls_ssl_conf_own_cert." ) );
                mbedtlsError = mbedtls_ssl_conf_own_cert( &( pSslContext->config ),
                                                          pNetworkCredentials->pClientCert,
                                                          pNetworkCredentials->pPrivateKey );
            }

            if( mbedtlsError == 0 )
            {
                LogInfo( ( "Before mbedtls_ssl_conf_dtls_cookies." ) );
                mbedtls_ssl_conf_dtls_cookies( &( pSslContext->config ),
                                               NULL,
                                               NULL,
                                               NULL );
            }

            if( mbedtlsError == 0 )
            {
                LogInfo( ( "Before mbedtls_ssl_conf_dtls_srtp_protection_profiles." ) );
                mbedtlsError = mbedtls_ssl_conf_dtls_srtp_protection_profiles( &pSslContext->config,
                                                                               DTLS_SRTP_SUPPORTED_PROFILES );
                if( mbedtlsError != 0 )
                {
                    LogError( ( "mbedtls_ssl_conf_dtls_srtp_protection_profiles failed" ) );
                    MBEDTLS_ERROR_DESCRIPTION( mbedtlsError );
                }
            }
            if( mbedtlsError == 0 )
            {
                LogInfo( ( "Before mbedtls_ssl_conf_export_keys_ext_cb." ) );

                mbedtls_ssl_conf_export_keys_ext_cb( &pSslContext->config,
                                                     dtlsSessionKeyDerivationCallback,
                                                     pSslContext );
            }
        }
        else
        {
            LogError( ( "pNetworkCredentials->pPrivateKey == NULL" ) );
            mbedtlsError = -1;
        }
    }
    else
    {
        LogError( ( "pNetworkCredentials->pClientCert == NULL" ) );
        mbedtlsError = -1;
    }

    return mbedtlsError;
}
/*-----------------------------------------------------------*/

static DtlsTransportStatus_t dtlsSetup( DtlsNetworkContext_t * pNetworkContext,
                                        DtlsNetworkCredentials_t * pNetworkCredentials )
{
    DtlsTransportParams_t * pDtlsTransportParams = NULL;
    DtlsTransportStatus_t returnStatus = DTLS_TRANSPORT_SUCCESS;
    int32_t mbedtlsError = 0;

    assert( pNetworkContext != NULL );
    assert( pNetworkContext->pParams != NULL );
    assert( pNetworkCredentials != NULL );
    // assert( pNetworkCredentials->pRootCa != NULL );

    pDtlsTransportParams = pNetworkContext->pParams;
    /* Initialize the mbed DTLS context structures. */
    DtlsSslContextInit( &( pDtlsTransportParams->dtlsSslContext ) );

    mbedtlsError = mbedtls_ssl_config_defaults( &( pDtlsTransportParams->dtlsSslContext.config ),
                                                MBEDTLS_SSL_IS_CLIENT,
                                                MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                                MBEDTLS_SSL_PRESET_DEFAULT );

    if( mbedtlsError != 0 )
    {
        LogError( ( "Failed to set default SSL configuration: mbedTLSError= %s : %s.", mbedtlsHighLevelCodeOrDefault( mbedtlsError ), mbedtlsLowLevelCodeOrDefault( mbedtlsError ) ) );

        /* Per mbed DTLS docs, mbedtls_ssl_config_defaults only fails on memory
         * allocation. */
        returnStatus = DTLS_TRANSPORT_INSUFFICIENT_MEMORY;
    }

    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        LogInfo( ( "Before setCredentials." ) );
        mbedtlsError = setCredentials( &( pDtlsTransportParams->dtlsSslContext ),
                                       pNetworkCredentials );

        if( mbedtlsError != 0 )
        {
            returnStatus = DTLS_TRANSPORT_INVALID_CREDENTIALS;
        }
    }

    return returnStatus;
}
/*-----------------------------------------------------------*/

static DtlsTransportStatus_t dtlsHandshake( DtlsNetworkContext_t * pNetworkContext,
                                            const DtlsNetworkCredentials_t * pNetworkCredentials )
{
    DtlsTransportParams_t * pDtlsTransportParams = NULL;
    DtlsTransportStatus_t returnStatus = DTLS_TRANSPORT_SUCCESS;
    int32_t mbedtlsError = 0;

    assert( pNetworkContext != NULL );
    assert( pNetworkContext->pParams != NULL );
    assert( pNetworkCredentials != NULL );

    pDtlsTransportParams = pNetworkContext->pParams;
    /* Initialize the mbed DTLS secured connection context. */
    mbedtlsError = mbedtls_ssl_setup( &( pDtlsTransportParams->dtlsSslContext.context ),
                                      &( pDtlsTransportParams->dtlsSslContext.config ) );

    if( mbedtlsError != 0 )
    {
        LogError( ( "Failed to set up mbed DTLS SSL context: mbedTLSError= %s : %s.", mbedtlsHighLevelCodeOrDefault( mbedtlsError ), mbedtlsLowLevelCodeOrDefault( mbedtlsError ) ) );

        returnStatus = DTLS_TRANSPORT_INTERNAL_ERROR;
    }
    else
    {
        /* Set the underlying IO for the DTLS connection. */

        /* MISRA Rule 11.2 flags the following line for casting the second
         * parameter to void *. This rule is suppressed because
         * #mbedtls_ssl_set_bio requires the second parameter as void *.
         */
        /* coverity[misra_c_2012_rule_11_2_violation] */

        /* These two macros MBEDTLS_SSL_SEND and MBEDTLS_SSL_RECV need to be
         * defined in mbedtls_config.h according to which implementation you
         * use.
         */
        mbedtls_ssl_set_bio( &( pDtlsTransportParams->dtlsSslContext.context ),
                             ( void * ) pDtlsTransportParams,
                             DtlsUdpSendWrap,
                             DtlsUdpRecvWrap,
                             NULL );
    }

    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        /* Perform the DTLS handshake. */
        do
        {
            mbedtlsError = mbedtls_ssl_handshake( &( pDtlsTransportParams->dtlsSslContext.context ) );
        } while( ( mbedtlsError == MBEDTLS_ERR_SSL_WANT_READ ) || ( mbedtlsError == MBEDTLS_ERR_SSL_WANT_WRITE ) );

        if( mbedtlsError != 0 )
        {
            LogError( ( "Failed to perform DTLS handshake: mbedTLSError= %s : %s.", mbedtlsHighLevelCodeOrDefault( mbedtlsError ), mbedtlsLowLevelCodeOrDefault( mbedtlsError ) ) );
            MBEDTLS_ERROR_DESCRIPTION( mbedtlsError );
            returnStatus = DTLS_TRANSPORT_HANDSHAKE_FAILED;
        }
        else
        {
            LogInfo( ( "(Network connection %p) DTLS handshake successful.", pNetworkContext ) );
        }
    }

    return returnStatus;
}
/*-----------------------------------------------------------*/

static DtlsTransportStatus_t initMbedtls( mbedtls_entropy_context * pEntropyContext,
                                          mbedtls_ctr_drbg_context * pCtrDrbgContext )
{
    DtlsTransportStatus_t returnStatus = DTLS_TRANSPORT_SUCCESS;
    int32_t mbedtlsError = 0;

#if defined( MBEDTLS_THREADING_ALT )
    /* Set the mutex functions for mbed DTLS thread safety. */
    mbedtls_platform_threading_init();
#endif

    /* Initialize contexts for random number generation. */
    mbedtls_entropy_init( pEntropyContext );
    mbedtls_ctr_drbg_init( pCtrDrbgContext );

    if( mbedtlsError != 0 )
    {
        LogError( ( "Failed to add entropy source: mbedTLSError= %s : %s.", mbedtlsHighLevelCodeOrDefault( mbedtlsError ), mbedtlsLowLevelCodeOrDefault( mbedtlsError ) ) );
        returnStatus = DTLS_TRANSPORT_INTERNAL_ERROR;
    }

#ifdef MBEDTLS_PSA_CRYPTO_C
    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        mbedtlsError = psa_crypto_init();

        if( mbedtlsError != PSA_SUCCESS )
        {
            LogError( ( "Failed to initialize PSA Crypto implementation: %d", ( int )mbedtlsError ) );
            returnStatus = DTLS_TRANSPORT_INTERNAL_ERROR;
        }
    }
#endif /* MBEDTLS_PSA_CRYPTO_C */

    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        /* Seed the random number generator. */
        mbedtlsError = mbedtls_ctr_drbg_seed( pCtrDrbgContext,
                                              mbedtls_entropy_func,
                                              pEntropyContext,
                                              NULL,
                                              0 );

        if( mbedtlsError != 0 )
        {
            LogError( ( "Failed to seed PRNG: mbedTLSError= %s : %s.", mbedtlsHighLevelCodeOrDefault( mbedtlsError ), mbedtlsLowLevelCodeOrDefault( mbedtlsError ) ) );
            returnStatus = DTLS_TRANSPORT_INTERNAL_ERROR;
        }
    }

    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        LogDebug( ( "Successfully initialized mbedTLS." ) );
    }

    return returnStatus;
}
/*-----------------------------------------------------------*/

DtlsTransportStatus_t
DTLS_Connect( DtlsNetworkContext_t * pNetworkContext,
              DtlsNetworkCredentials_t * pNetworkCredentials,
              const char * pHostName,
              uint16_t port )
{
    DtlsTransportParams_t * pDtlsTransportParams = NULL;
    DtlsTransportStatus_t returnStatus = DTLS_TRANSPORT_SUCCESS;
    int32_t socketStatus = 0;
    int32_t isSocketConnected = 0, isTlsSetup = 0;

    if( NULL == pNetworkCredentials->pClientCert )
    {
        LogError( ( "NULL == pNetworkCredentials->pClientCert" ) );
        returnStatus = DTLS_TRANSPORT_INVALID_PARAMETER;
    }

    if( NULL == pNetworkCredentials->pPrivateKey )
    {
        LogError( ( "NULL == pNetworkCredentials->pPrivateKey" ) );
        returnStatus = DTLS_TRANSPORT_INVALID_PARAMETER;
    }

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) || ( pNetworkCredentials == NULL ) || ( pHostName == NULL ) )
    {
        LogError( ( "Invalid input parameter(s): Arguments cannot be NULL. "
                    "pNetworkContext=%p, "
                    "pNetworkCredentials=%p,"
                    " pHostName: %s.",
                    pNetworkContext,
                    pNetworkCredentials,
                    pHostName ) );
        returnStatus = DTLS_TRANSPORT_INVALID_PARAMETER;
    }
    else
    {
        /* Empty else for MISRA 15.7 compliance. */
    }

    /* Initialize mbedtls. */
    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        pDtlsTransportParams = pNetworkContext->pParams;

        returnStatus = initMbedtls( &( pDtlsTransportParams->dtlsSslContext.entropyContext ),
                                    &( pDtlsTransportParams->dtlsSslContext.ctrDrbgContext ) );
    }

    /* Establish a UDP connection with the server. */
    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        pDtlsTransportParams = pNetworkContext->pParams;

        socketStatus = UDP_Sockets_Connect( &( pDtlsTransportParams->udpSocket ),
                                            pHostName,
                                            port,
                                            1000,
                                            1000 );

        if( socketStatus != 0 )
        {
            LogError( ( "Failed to connect to %s with error %d.", pHostName, socketStatus ) );
            returnStatus = DTLS_TRANSPORT_CONNECT_FAILURE;
        }
    }

    /* Initialize DTLS contexts and set credentials. */
    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        isSocketConnected = 1;

        returnStatus = dtlsSetup( pNetworkContext,
                                  pNetworkCredentials );
    }

    memset( &pNetworkContext->pParams->mbedtlsTimer,
            0,
            sizeof( mbedtls_timing_delay_context ) );

    /* Set the timer functions for mbed DTLS. */
    mbedtls_ssl_set_timer_cb( &pNetworkContext->pParams->dtlsSslContext.context,
                              &pNetworkContext->pParams->mbedtlsTimer,
                              &mbedtls_timing_set_delay,
                              &mbedtls_timing_get_delay );

    /* Perform DTLS handshake. */
    if( returnStatus == DTLS_TRANSPORT_SUCCESS )
    {
        isTlsSetup = 1;

        returnStatus = dtlsHandshake( pNetworkContext,
                                      pNetworkCredentials );
    }

    /* Clean up on failure. */
    if( returnStatus != DTLS_TRANSPORT_SUCCESS )
    {
        /* Free SSL context if it's setup. */
        if( isTlsSetup == 1 )
        {
            DtlsSslContextFree( &( pDtlsTransportParams->dtlsSslContext ) );
        }

        /* Call Sockets_Disconnect if socket was connected. */
        if( isSocketConnected == 1 )
        {
            UDP_Sockets_Disconnect( pDtlsTransportParams->udpSocket );
            pDtlsTransportParams->udpSocket = NULL;
        }
    }
    else
    {
        LogInfo( ( "(Network connection %p) Connection to %s established.", pNetworkContext, pHostName ) );
    }

    return returnStatus;
}
/*-----------------------------------------------------------*/

void DTLS_Disconnect( DtlsNetworkContext_t * pNetworkContext )
{
    DtlsTransportParams_t * pTlsTransportParams = NULL;
    int32_t dtlsStatus = 0;

    if( ( pNetworkContext != NULL ) && ( pNetworkContext->pParams != NULL ) )
    {
        pTlsTransportParams = pNetworkContext->pParams;
        /* Attempting to terminate DTLS connection. */
        dtlsStatus = ( int32_t )mbedtls_ssl_close_notify( &( pTlsTransportParams->dtlsSslContext.context ) );

        /* Ignore the WANT_READ and WANT_WRITE return values. */
        if( ( dtlsStatus != ( int32_t )MBEDTLS_ERR_SSL_WANT_READ ) && ( dtlsStatus != ( int32_t )MBEDTLS_ERR_SSL_WANT_WRITE ) )
        {
            if( dtlsStatus == 0 )
            {
                LogInfo( ( "(Network connection %p) DTLS close-notify sent.", pNetworkContext ) );
            }
            else
            {
                LogError( ( "(Network connection %p) Failed to send DTLS close-notify: "
                            "mbedTLSError= %s : %s.",
                            pNetworkContext,
                            mbedtlsHighLevelCodeOrDefault( dtlsStatus ),
                            mbedtlsLowLevelCodeOrDefault( dtlsStatus ) ) );
            }
        }
        else
        {
            /* WANT_READ and WANT_WRITE can be ignored. Logging for debugging purposes. */
            LogInfo( ( "(Network connection %p) TLS close-notify sent; "
                       "received %s as the TLS status can be ignored for close-notify.",
                       pNetworkContext,
                       ( dtlsStatus == MBEDTLS_ERR_SSL_WANT_READ ) ? "WANT_READ" : "WANT_WRITE" ) );
        }

        /* Free mbed DTLS contexts. */
        DtlsSslContextFree( &( pTlsTransportParams->dtlsSslContext ) );
    }
}
/*-----------------------------------------------------------*/

int32_t DTLS_recv( DtlsNetworkContext_t * pNetworkContext,
                   void * pBuffer,
                   size_t bytesToRecv )
{
    DtlsTransportParams_t * pDtlsTransportParams = NULL;
    int32_t dtlsStatus = 0;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        LogError( ( "invalid input, pNetworkContext=%p", pNetworkContext ) );
        dtlsStatus = -1;
    }
    else if( pBuffer == NULL )
    {
        LogError( ( "invalid input, pBuffer == NULL" ) );
        dtlsStatus = -1;
    }
    else if( bytesToRecv == 0 )
    {
        LogError( ( "invalid input, bytesToRecv == 0" ) );
        dtlsStatus = -1;
    }
    else
    {
        pDtlsTransportParams = pNetworkContext->pParams;

        dtlsStatus = ( int32_t )mbedtls_ssl_read( &( pDtlsTransportParams->dtlsSslContext.context ),
                                                  pBuffer,
                                                  bytesToRecv );

        if( ( dtlsStatus == MBEDTLS_ERR_SSL_TIMEOUT ) || ( dtlsStatus == MBEDTLS_ERR_SSL_WANT_READ ) || ( dtlsStatus == MBEDTLS_ERR_SSL_WANT_WRITE ) )
        {
            LogDebug( ( "Failed to read data. However, a read can be retried on "
                        "this error. "
                        "mbedTLSError= %s : %s.",
                        mbedtlsHighLevelCodeOrDefault( dtlsStatus ),
                        mbedtlsLowLevelCodeOrDefault( dtlsStatus ) ) );

            /* Mark these set of errors as a timeout. The libraries may retry
             * read on these errors. */
            dtlsStatus = 0;
        }
        else if( dtlsStatus < 0 )
        {
            LogError( ( "Failed to read data: mbedTLSError= %s : %s.", mbedtlsHighLevelCodeOrDefault( dtlsStatus ), mbedtlsLowLevelCodeOrDefault( dtlsStatus ) ) );
        }
        else
        {
            /* Empty else marker. */
        }
    }

    return dtlsStatus;
}
/*-----------------------------------------------------------*/

int32_t DTLS_send( DtlsNetworkContext_t * pNetworkContext,
                   const void * pBuffer,
                   size_t bytesToSend )
{
    DtlsTransportParams_t * pDtlsTransportParams = NULL;
    int32_t dtlsStatus = 0;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        LogError( ( "invalid input, pNetworkContext=%p", pNetworkContext ) );
        dtlsStatus = -1;
    }
    else if( pBuffer == NULL )
    {
        LogError( ( "invalid input, pBuffer == NULL" ) );
        dtlsStatus = -1;
    }
    else if( bytesToSend == 0 )
    {
        LogError( ( "invalid input, bytesToSend == 0" ) );
        dtlsStatus = -1;
    }
    else
    {
        pDtlsTransportParams = pNetworkContext->pParams;

        dtlsStatus = ( int32_t )mbedtls_ssl_write( &( pDtlsTransportParams->dtlsSslContext.context ),
                                                   pBuffer,
                                                   bytesToSend );

        if( ( dtlsStatus == MBEDTLS_ERR_SSL_TIMEOUT ) || ( dtlsStatus == MBEDTLS_ERR_SSL_WANT_READ ) || ( dtlsStatus == MBEDTLS_ERR_SSL_WANT_WRITE ) )
        {
            LogDebug( ( "Failed to send data. However, send can be retried on "
                        "this error. "
                        "mbedTLSError= %s : %s.",
                        mbedtlsHighLevelCodeOrDefault( dtlsStatus ),
                        mbedtlsLowLevelCodeOrDefault( dtlsStatus ) ) );

            /* Mark these set of errors as a timeout. The libraries may retry
             * send on these errors. */
            dtlsStatus = 0;
        }
        else if( dtlsStatus < 0 )
        {
            LogError( ( "Failed to send data:  mbedTLSError= %s : %s.", mbedtlsHighLevelCodeOrDefault( dtlsStatus ), mbedtlsLowLevelCodeOrDefault( dtlsStatus ) ) );
        }
        else
        {
            /* Empty else marker. */
        }
    }

    return dtlsStatus;
}
/*-----------------------------------------------------------*/

int32_t dtlsFillPseudoRandomBits( uint8_t * pBuf,
                                  size_t bufSize )
{
    int32_t retStatus = 0;
    uint32_t i;

    if( ( bufSize >= DTLS_CERT_MIN_SERIAL_NUM_SIZE ) && ( bufSize <= DTLS_CERT_MAX_SERIAL_NUM_SIZE ) )
    {
        if( pBuf != NULL )
        {

            for( i = 0; i < bufSize; i++ )
            {
                *pBuf++ = ( uint8_t )( rand() & 0xFF );
            }
        }
        else
        {
            retStatus = STATUS_NULL_ARG;
        }
    }
    else
    {
        retStatus = STATUS_INVALID_ARG;
        LogError( ( "invalid input, bufSize >= DTLS_CERT_MIN_SERIAL_NUM_SIZE && "
                    "bufSize <= DTLS_CERT_MAX_SERIAL_NUM_SIZE " ) );
    }
    return retStatus;
}
/*-----------------------------------------------------------*/

int32_t dtlsCreateCertificateFingerprint( const mbedtls_x509_crt * pCert,
                                          char * pBuff,
                                          const size_t bufLen )
{
    int32_t retStatus = 0;
    uint8_t fingerprint[MBEDTLS_MD_MAX_SIZE];
    int32_t sslRet, i, size;
    // const is not pure C, but mbedtls_md_info_from_type requires the param to
    // be const
    const mbedtls_md_info_t * pMdInfo;

    if( ( pBuff == NULL ) )
    {
        LogError( ( "invalid input, pBuff == NULL " ) );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    pMdInfo = mbedtls_md_info_from_type( MBEDTLS_MD_SHA256 );
    if( ( pMdInfo == NULL ) )
    {
        LogError( ( "invalid input, pMdInfo == NULL " ) );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    sslRet = mbedtls_sha256_ret( pCert->raw.p,
                                 pCert->raw.len,
                                 fingerprint,
                                 0 );
    if( sslRet != 0 )
    {
        LogError( ( "Failed to calculate the SHA-256 checksum: mbedTLSError= %s : %s.", mbedtlsHighLevelCodeOrDefault( sslRet ), mbedtlsLowLevelCodeOrDefault( sslRet ) ) );
    }
    else
    {
        /* Empty else marker. */
    }

    size = mbedtls_md_get_size( pMdInfo );

    if( bufLen < 3 * size )
    {
        LogError( ( "buffer to store fingerprint too small buffer: %lu size: %d", bufLen, size ) );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    for( i = 0; i < size; i++ )
    {
        sprintf( pBuff,
                 "%.2X:",
                 fingerprint[i] );
        pBuff += 3;
    }
    *( pBuff - 1 ) = '\0';

    return retStatus;
}
/*-----------------------------------------------------------*/

int32_t dtlsSessionGetLocalCertificateFingerprint( DtlsSSLContext_t * pSslContext,
                                                   void * pBuff,
                                                   size_t buffLen )
{
    int32_t retStatus = 0;

    if( ( pSslContext == NULL ) || ( pBuff == NULL ) )
    {
        LogError( ( "invalid input, pSslContext || pBuff == NULL " ) );
        retStatus = -1;
    }
    else if( buffLen < CERTIFICATE_FINGERPRINT_LENGTH )
    {
        LogError( ( "buffLen < CERTIFICATE_FINGERPRINT_LENGTH " ) );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    dtlsCreateCertificateFingerprint( &pSslContext->clientCert,
                                      pBuff,
                                      buffLen );

    return retStatus;
}
/*-----------------------------------------------------------*/

int32_t dtlsSessionVerifyRemoteCertificateFingerprint( DtlsSSLContext_t * pSslContext,
                                                       char * pExpectedFingerprint,
                                                       const size_t fingerprintMaxLen )
{
    int32_t retStatus = 0;
    char actualFingerprint[ CERTIFICATE_FINGERPRINT_LENGTH ];
    mbedtls_x509_crt * pRemoteCertificate = NULL;

    if( ( pSslContext == NULL ) || ( pExpectedFingerprint == NULL ) || ( CERTIFICATE_FINGERPRINT_LENGTH < fingerprintMaxLen ) )
    {
        LogError( ( "invalid input, pSslContext || pExpectedFingerprint == NULL || CERTIFICATE_FINGERPRINT_LENGTH < fingerprintMaxLen(%lu)", fingerprintMaxLen ) );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    pRemoteCertificate = ( mbedtls_x509_crt * )mbedtls_ssl_get_peer_cert( &pSslContext->context );
    if( ( pRemoteCertificate == NULL ) )
    {
        LogError( ( "pRemoteCertificate == NULL " ) );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    if( dtlsCreateCertificateFingerprint( pRemoteCertificate,
                                          actualFingerprint,
                                          CERTIFICATE_FINGERPRINT_LENGTH ) != 0 )
    {
        LogError( ( "Failed to calculate certificate fingerprint" ) );
    }
    else
    {
        /* Empty else marker. */
    }

    if( strncmp( pExpectedFingerprint,
                 actualFingerprint,
                 fingerprintMaxLen ) != 0 )
    {
        LogError( ( "STATUS_SSL_REMOTE_CERTIFICATE_VERIFICATION_FAILED \nexpected fingerprint:\n %s \nactual fingerprint:\n %s", pExpectedFingerprint, actualFingerprint ) );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    return retStatus;
}
/*-----------------------------------------------------------*/

int32_t dtlsSessionPopulateKeyingMaterial( DtlsSSLContext_t * pSslContext,
                                           pDtlsKeyingMaterial_t pDtlsKeyingMaterial )
{
    int32_t retStatus = 0;
    uint32_t offset = 0;

    TlsKeys * pKeys = NULL;
    uint8_t keyingMaterialBuffer[MAX_SRTP_MASTER_KEY_LEN * 2 + MAX_SRTP_SALT_KEY_LEN * 2];
#if ( MBEDTLS_VERSION_NUMBER > 0x02100600 )
//#if ( MBEDTLS_VERSION_NUMBER ==  || MBEDTLS_VERSION_NUMBER == 0x03020100 )
    mbedtls_dtls_srtp_info negotiatedSRTPProfile;
#else
    mbedtls_ssl_srtp_profile negotiatedSRTPProfile;
#endif

    if( ( pSslContext == NULL ) || ( pDtlsKeyingMaterial == NULL ) )
    {
        LogError( ( "invalid input, pSslContext || pDtlsKeyingMaterial == NULL " ) );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    pKeys = ( TlsKeys * ) &pSslContext->tlsKeys;

    // https://mbed-tls.readthedocs.io/en/latest/kb/how-to/tls_prf/
    retStatus = mbedtls_ssl_tls_prf( pKeys->tlsProfile,
                                     pKeys->masterSecret,
                                     ARRAY_SIZE( pKeys->masterSecret ),
                                     KEYING_EXTRACTOR_LABEL,
                                     pKeys->randBytes,
                                     ARRAY_SIZE( pKeys->randBytes ),
                                     keyingMaterialBuffer,
                                     ARRAY_SIZE( keyingMaterialBuffer ) );
    if( retStatus != 0 )
    {
        LogError( ( "Failed TLS-PRF function for key derivation, funct: %d", pKeys->tlsProfile ) );
        MBEDTLS_ERROR_DESCRIPTION( retStatus );
        retStatus = -1;
    }
    else
    {
        /* Empty else marker. */
    }

    if( retStatus == 0 )
    {
        pDtlsKeyingMaterial->key_length = MAX_SRTP_MASTER_KEY_LEN + MAX_SRTP_SALT_KEY_LEN;

        memcpy( pDtlsKeyingMaterial->clientWriteKey,
                &keyingMaterialBuffer[offset],
                MAX_SRTP_MASTER_KEY_LEN );
        offset += MAX_SRTP_MASTER_KEY_LEN;

        memcpy( pDtlsKeyingMaterial->serverWriteKey,
                &keyingMaterialBuffer[offset],
                MAX_SRTP_MASTER_KEY_LEN );
        offset += MAX_SRTP_MASTER_KEY_LEN;

        memcpy( pDtlsKeyingMaterial->clientWriteKey + MAX_SRTP_MASTER_KEY_LEN,
                &keyingMaterialBuffer[offset],
                MAX_SRTP_SALT_KEY_LEN );
        offset += MAX_SRTP_SALT_KEY_LEN;

        memcpy( pDtlsKeyingMaterial->serverWriteKey + MAX_SRTP_MASTER_KEY_LEN,
                &keyingMaterialBuffer[offset],
                MAX_SRTP_SALT_KEY_LEN );
#if ( MBEDTLS_VERSION_NUMBER > 0x02100600 )
//#if ( MBEDTLS_VERSION_NUMBER == 0x03000000 || MBEDTLS_VERSION_NUMBER == 0x03020100 )
        mbedtls_ssl_get_dtls_srtp_negotiation_result( &pSslContext->context, &negotiatedSRTPProfile );
        switch( negotiatedSRTPProfile.chosen_dtls_srtp_profile )
        {
            case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80:
#else
        negotiatedSRTPProfile = mbedtls_ssl_get_dtls_srtp_protection_profile( &pSslContext->context );
        switch( negotiatedSRTPProfile )
        {
            case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80:
#endif
                pDtlsKeyingMaterial->srtpProfile = KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_80;
                break;

#if ( MBEDTLS_VERSION_NUMBER == 0x03000000 || MBEDTLS_VERSION_NUMBER == 0x03020100 )
            case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32:
#else
            case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32:
#endif
                pDtlsKeyingMaterial->srtpProfile = KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_32;
                break;
            default:
                LogError( ( "STATUS_SSL_UNKNOWN_SRTP_PROFILE" ) );
                retStatus = -1;
        }
    }
    return retStatus;
}
/*-----------------------------------------------------------*/

/**
 * createCertificateAndKey generates a new certificate and a key
 * If generateRSACertificate is true, RSA is going to be used for the key
 * generation. Otherwise, ECDSA is going to be used. certificateBits is only
 * being used when generateRSACertificate is true.
 */
int32_t createCertificateAndKey( int32_t certificateBits,
                                 int32_t generateRSACertificate,
                                 mbedtls_x509_crt * pCert,
                                 mbedtls_pk_context * pKey )
{
    int32_t retStatus = 0;
    int32_t initialized = 0;
    char * pCertBuf = NULL;
    char notBeforeBuf[MBEDTLS_X509_RFC5280_UTC_TIME_LEN + 1], notAfterBuf[MBEDTLS_X509_RFC5280_UTC_TIME_LEN + 1];
    // TODO RTC needs to be solved: uint64_t now, notAfter;
    int32_t len;
    mbedtls_entropy_context * pEntropy = NULL;
    mbedtls_ctr_drbg_context * pCtrDrbg = NULL;
    mbedtls_mpi serial;
    mbedtls_x509write_cert * pWriteCert = NULL;
    uint8_t certSn[DTLS_CERT_MAX_SERIAL_NUM_SIZE];

    if( ( pCert != NULL ) && ( pKey != NULL ) )
    {
        if( ( pCertBuf = ( char * )malloc( GENERATED_CERTIFICATE_MAX_SIZE ) ) )
        {
            if( ( NULL != ( pEntropy = ( mbedtls_entropy_context * )malloc( sizeof( mbedtls_entropy_context ) ) ) ) )
            {
                if( ( NULL != ( pCtrDrbg = ( mbedtls_ctr_drbg_context * )malloc( sizeof( mbedtls_ctr_drbg_context ) ) ) ) )
                {
                    if( ( NULL != ( pWriteCert = ( mbedtls_x509write_cert * )malloc( sizeof( mbedtls_x509write_cert ) ) ) ) )
                    {
                        if( dtlsFillPseudoRandomBits( certSn,
                                                      sizeof( certSn ) ) == 0 )
                        {
                            // initialize to sane values
                            mbedtls_entropy_init( pEntropy );
                            mbedtls_ctr_drbg_init( pCtrDrbg );
                            mbedtls_mpi_init( &serial );
                            mbedtls_x509write_crt_init( pWriteCert );
                            mbedtls_x509_crt_init( pCert );
                            mbedtls_pk_init( pKey );
                            initialized = 1;
                            if( mbedtls_ctr_drbg_seed( pCtrDrbg,
                                                       mbedtls_entropy_func,
                                                       pEntropy,
                                                       NULL,
                                                       0 ) == 0 )
                            {
                                LogDebug( ( "mbedtls_ctr_drbg_seed successful" ) );

                                // generate a RSA key
                                if( generateRSACertificate )
                                {
                                    LogWarn( ( "generateRSACertificate this will take about 10mins" ) );

                                    if( mbedtls_pk_setup( pKey,
                                                          mbedtls_pk_info_from_type( MBEDTLS_PK_RSA ) ) == 0 )
                                    {
                                        LogDebug( ( "mbedtls_pk_setup successful" ) );
                                        if( mbedtls_rsa_gen_key( mbedtls_pk_rsa( *pKey ),
                                                                 mbedtls_ctr_drbg_random,
                                                                 pCtrDrbg,
                                                                 certificateBits,
                                                                 DTLS_RSA_F4 ) == 0 )
                                        {
                                            LogDebug( ( "mbedtls_rsa_gen_key successful" ) );
                                        }
                                        else
                                        {
                                            retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                            LogError( ( "mbedtls_rsa_gen_key failed" ) );
                                        }
                                    }
                                    else
                                    {
                                        retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                        LogError( ( "mbedtls_pk_setup STATUS_CERTIFICATE_GENERATION_FAILED" ) );
                                    }
                                }
                                else     // generate ECDSA
                                {

                                    if( ( mbedtls_pk_setup( pKey,
                                                            mbedtls_pk_info_from_type( MBEDTLS_PK_ECKEY ) ) == 0 ) &&
                                        ( mbedtls_ecp_gen_key( MBEDTLS_ECP_DP_SECP256R1,
                                                               mbedtls_pk_ec( *pKey ),
                                                               mbedtls_ctr_drbg_random,
                                                               pCtrDrbg ) == 0 ) )
                                    {
                                        LogDebug( ( "mbedtls_pk_setup && mbedtls_ecp_gen_key successful" ) );
                                    }
                                    else
                                    {
                                        retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                        LogError( ( "mbedtls_pk_setup or mbedtls_rsa_gen_key" ) );
                                    }
                                }
                            }

                            // generate a new certificate
                            int mbedtlsRet = mbedtls_mpi_read_binary( &serial,
                                                                      certSn,
                                                                      sizeof( certSn ) );
                            if( mbedtlsRet == 0 )
                            {
                                LogDebug( ( "mbedtls_mpi_read_binary successful" ) );
                                struct timespec nowTime;
                                time_t timeT;
                                clock_gettime( CLOCK_REALTIME, &nowTime );
                                timeT = nowTime.tv_sec;

                                if( strftime( notBeforeBuf,
                                              sizeof( notBeforeBuf ),
                                              "%Y%m%d%H%M%S",
                                              gmtime( &timeT ) ) != MBEDTLS_X509_RFC5280_UTC_TIME_LEN )
                                {
                                    LogDebug( ( "notBefore: %s", notBeforeBuf ) );

                                    // notAfter = now + GENERATED_CERTIFICATE_DAYS *
                                    // HUNDREDS_OF_NANOS_IN_A_DAY;
                                    timeT = nowTime.tv_sec + ( GENERATED_CERTIFICATE_DAYS * DTLS_SECONDS_IN_A_DAY );
                                    if( strftime( notAfterBuf,
                                                  sizeof( notAfterBuf ),
                                                  "%Y%m%d%H%M%S",
                                                  gmtime( &timeT ) ) != MBEDTLS_X509_RFC5280_UTC_TIME_LEN )
                                    {
                                        LogDebug( ( "notAfter: %s", notAfterBuf ) );

                                        if( mbedtls_x509write_crt_set_serial( pWriteCert,
                                                                              &serial ) == 0 )
                                        {
                                            if( mbedtls_x509write_crt_set_validity( pWriteCert,
                                                                                    notBeforeBuf,
                                                                                    notAfterBuf ) == 0 )
                                            {
                                                if( mbedtls_x509write_crt_set_subject_name( pWriteCert,
                                                                                            "O"
                                                                                            "=" GENERATED_CERTIFICATE_NAME ",CN"
                                                                                            "=" GENERATED_CERTIFICATE_NAME ) == 0 )
                                                {
                                                    if( mbedtls_x509write_crt_set_issuer_name( pWriteCert,
                                                                                               "O"
                                                                                               "=" GENERATED_CERTIFICATE_NAME ",CN"
                                                                                               "=" GENERATED_CERTIFICATE_NAME ) != 0 )
                                                    {
                                                        retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                                        LogError( ( "mbedtls_x509write_crt_set_issuer_name failed" ) );
                                                    }
                                                }
                                            }
                                            else
                                            {
                                                retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                                LogError( ( "mbedtls_x509write_crt_set_validity failed" ) );
                                            }

                                            // void functions, it must succeed
                                            mbedtls_x509write_crt_set_version( pWriteCert,
                                                                               MBEDTLS_X509_CRT_VERSION_3 );
                                            mbedtls_x509write_crt_set_subject_key( pWriteCert,
                                                                                   pKey );
                                            mbedtls_x509write_crt_set_issuer_key( pWriteCert,
                                                                                  pKey );
                                            mbedtls_x509write_crt_set_md_alg( pWriteCert,
                                                                              MBEDTLS_MD_SHA1 );

                                            memset( pCertBuf,
                                                    0,
                                                    GENERATED_CERTIFICATE_MAX_SIZE );
                                            len = mbedtls_x509write_crt_der( pWriteCert,
                                                                             ( void * )pCertBuf,
                                                                             GENERATED_CERTIFICATE_MAX_SIZE,
                                                                             mbedtls_ctr_drbg_random,
                                                                             pCtrDrbg );
                                            LogDebug( ( "mbedtls_x509write_crt_der, len: %d", len ) );
                                            if( len <= 0 )
                                            {
                                                retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                                LogError( ( "mbedtls_x509write_crt_der STATUS_CERTIFICATE_GENERATION_FAILED" ) );
                                            }

                                            // mbedtls_x509write_crt_der starts
                                            // writing from behind, so we need to
                                            // use the return len to figure out
                                            // where the data actually starts:
                                            //
                                            //         -----------------------------------------
                                            //         |  padding      | certificate
                                            //         |
                                            //         -----------------------------------------
                                            //         ^               ^
                                            //       pCertBuf   pCertBuf +
                                            //       (sizeof(pCertBuf) - len)
                                            if( mbedtls_x509_crt_parse_der( pCert,
                                                                            ( void * )( pCertBuf + GENERATED_CERTIFICATE_MAX_SIZE - len ),
                                                                            len ) != 0 )
                                            {
                                                retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                                LogError( ( "mbedtls_x509_crt_parse_der failed" ) );
                                            }
                                        }
                                        else
                                        {
                                            retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                            LogError( ( "mbedtls_x509write_crt_set_serial failed" ) );
                                        }
                                    }
                                    else
                                    {
                                        retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                        LogError( ( "generateTimestampStr failed" ) );
                                    }
                                }
                                else
                                {
                                    retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                    LogError( ( "generateTimestampStr failed" ) );
                                }
                            }
                            else
                            {
                                retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                                LogError( ( "mbedtls_mpi_read_binary failed, ret: %d", mbedtlsRet ) );
                                MBEDTLS_ERROR_DESCRIPTION( mbedtlsRet );
                            }
                        }
                        else
                        {
                            retStatus = STATUS_CERTIFICATE_GENERATION_FAILED;
                            LogError( ( "dtlsFillPseudoRandomBits failed" ) );
                        }
                    }
                    else
                    {
                        retStatus = STATUS_NOT_ENOUGH_MEMORY;
                        LogError( ( "mbedtls_x509write_cert alloc failed" ) );
                    }
                }
                else
                {
                    retStatus = STATUS_NOT_ENOUGH_MEMORY;
                    LogError( ( "mbedtls_ctr_drbg_context alloc failed" ) );
                }
            }
            else
            {
                retStatus = STATUS_NOT_ENOUGH_MEMORY;
                LogError( ( "mbedtls_entropy_context alloc failed" ) );
            }
        }
        else
        {
            retStatus = STATUS_NOT_ENOUGH_MEMORY;
            LogError( ( "pCertBuf alloc failed" ) );
        }
    }
    else
    {
        LogError( ( "pCert != NULL && pKey != NULL" ) );
        retStatus = STATUS_NULL_ARG;
    }

    if( initialized && ( 0 != retStatus ) )
    {
        mbedtls_x509write_crt_free( pWriteCert );
        mbedtls_mpi_free( &serial );
        mbedtls_ctr_drbg_free( pCtrDrbg );
        mbedtls_entropy_free( pEntropy );

        if( 0 != retStatus )
        {
            freeCertificateAndKey( pCert,
                                   pKey );
        }
    }
    free( pCertBuf );
    free( pEntropy );
    free( pCtrDrbg );
    free( pWriteCert );

    return retStatus;
}
/*-----------------------------------------------------------*/


int32_t freeCertificateAndKey( mbedtls_x509_crt * pCert,
                               mbedtls_pk_context * pKey )
{
    int32_t dtlsStatus = STATUS_SUCCESS;

    if( pCert != NULL )
    {
        mbedtls_x509_crt_free( pCert );
    }
    else
    {
        dtlsStatus = STATUS_NULL_ARG;
    }

    if( pKey != NULL )
    {
        mbedtls_pk_free( pKey );
    }
    else
    {
        dtlsStatus = STATUS_NULL_ARG;
    }
    return dtlsStatus;
}
/*-----------------------------------------------------------*/
