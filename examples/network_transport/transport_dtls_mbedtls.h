/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/**
 * @file transport_dtls_mbedtls.h
 * @brief DTLS transport interface header.
 */

#ifndef TRANSPORT_DTLS_MBEDTLS_H
#define TRANSPORT_DTLS_MBEDTLS_H

#include "mbedtls/config.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/threading.h"
#include "mbedtls/x509.h"
#include "mbedtls/timing.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE( array ) ( sizeof( array ) / sizeof *( array ) )
#endif

#define MBEDTLS_ERROR_STRING_BUFFER_SIZE 512

#define MBEDTLS_ERROR_DESCRIPTION( err ) \
    do { \
        char _error_string[MBEDTLS_ERROR_STRING_BUFFER_SIZE]; \
        mbedtls_strerror( err, \
                          _error_string, \
                          sizeof( _error_string ) ); \
        LogError( ( "Error 0x%04x: %s\n", ( unsigned int )-( err ), _error_string ) ); \
    } while( 0 )

/* Include header that defines log levels. */
#include "logging.h"

/* UDP Sockets Wrapper include.*/
#include "udp_sockets_wrapper.h"

/* Transport interface include. */
#include "transport_interface.h"


/*! \addtogroup DTLSStatusCodes
 * WEBRTC DTLS related codes. Values are derived from STATUS_DTLS_BASE (0x59000000)
 *  @{
 */
#define STATUS_WEBRTC_BASE 0x55000000
#define STATUS_SDP_BASE STATUS_WEBRTC_BASE + 0x01000000
#define STATUS_STUN_BASE STATUS_SDP_BASE + 0x01000000
#define STATUS_NETWORKING_BASE STATUS_STUN_BASE + 0x01000000
#define STATUS_DTLS_BASE STATUS_NETWORKING_BASE + 0x01000000
#define STATUS_CERTIFICATE_GENERATION_FAILED STATUS_DTLS_BASE + 0x00000001
#define STATUS_SSL_CTX_CREATION_FAILED STATUS_DTLS_BASE + 0x00000002
#define STATUS_SSL_REMOTE_CERTIFICATE_VERIFICATION_FAILED STATUS_DTLS_BASE + 0x00000003
#define STATUS_SSL_PACKET_BEFORE_DTLS_READY STATUS_DTLS_BASE + 0x00000004
#define STATUS_SSL_UNKNOWN_SRTP_PROFILE STATUS_DTLS_BASE + 0x00000005
#define STATUS_SSL_INVALID_CERTIFICATE_BITS STATUS_DTLS_BASE + 0x00000006
#define STATUS_DTLS_SESSION_ALREADY_FREED STATUS_DTLS_BASE + 0x00000007
/*!@} */

/* SRTP */
#define CERTIFICATE_FINGERPRINT_LENGTH 160
#define MAX_SRTP_MASTER_KEY_LEN 16
#define MAX_SRTP_SALT_KEY_LEN 14
#define MAX_DTLS_RANDOM_BYTES_LEN 32
#define MAX_DTLS_MASTER_KEY_LEN 48

#define KEYING_EXTRACTOR_LABEL "EXTRACTOR-dtls_srtp"

typedef int (* OnTransportDtlsSendHook)( void *pCustomContext,
                                         const uint8_t *pInputBuffer,
                                         size_t inputBufferLength,
                                         uint8_t *pOutputBuffer,
                                         size_t *pOutputBufferSize );
typedef int (* OnTransportDtlsRecvHook)( void *pCustomContext,
                                         const uint8_t *pInputBuffer,
                                         size_t inputBufferLength,
                                         uint8_t *pOutputBuffer,
                                         size_t *pOutputBufferSize );

/*
 * For code readability use a typedef for DTLS-SRTP profiles
 *
 * Use_srtp extension protection profiles values as defined in
 * http://www.iana.org/assignments/srtp-protection/srtp-protection.xhtml
 *
 * Reminder: if this list is expanded mbedtls_ssl_check_srtp_profile_value
 * must be updated too.
 */
// #define MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80     ( ( uint16_t ) 0x0001 )
// #define MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32     ( ( uint16_t ) 0x0002 )
// #define MBEDTLS_TLS_SRTP_NULL_HMAC_SHA1_80          ( ( uint16_t ) 0x0005 )
// #define MBEDTLS_TLS_SRTP_NULL_HMAC_SHA1_32          ( ( uint16_t ) 0x0006 )

/* This one is not iana defined, but for code readability. */
//#define MBEDTLS_TLS_SRTP_UNSET                      ( ( uint16_t ) 0x0000 )

typedef enum
{
    KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_80 = MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80,
    KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_32 = MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32,
} KVS_SRTP_PROFILE;

typedef struct
{
    uint8_t masterSecret[MAX_DTLS_MASTER_KEY_LEN];
    // client random bytes + server random bytes
    uint8_t randBytes[2 * MAX_DTLS_RANDOM_BYTES_LEN];
    mbedtls_tls_prf_types tlsProfile;
} TlsKeys;



/**
 * @brief Secured connection context.
 */
typedef struct DtlsSSLContext
{
    mbedtls_ssl_config config;               /**< @brief SSL connection configuration. */
    mbedtls_ssl_context context;             /**< @brief SSL connection context */
    mbedtls_x509_crt_profile certProfile;    /**< @brief Certificate security profile for this connection. */
    mbedtls_x509_crt rootCa;                 /**< @brief Root CA certificate context. */
    mbedtls_x509_crt clientCert;             /**< @brief Client certificate context. */
    mbedtls_pk_context privKey;              /**< @brief Client private key context. */
    TlsKeys tlsKeys;                         /**< @brief Client private key context. */
    mbedtls_entropy_context entropyContext;  /**< @brief Entropy context for random number generation. */
    mbedtls_ctr_drbg_context ctrDrbgContext; /**< @brief CTR DRBG context for random number generation. */
} DtlsSSLContext_t;

typedef void (* mbedtls_set_delay_fptr)( void *,
                                         uint32_t,
                                         uint32_t );
typedef int (* mbedtls_get_delay_fptr)( void * );

typedef struct DtlsSessionTimer
{
    uint32_t int_ms;                  // Intermediate delay in milliseconds
    uint32_t fin_ms;                  // Final delay in milliseconds
    int64_t start_ticks;              // Start tick count
    mbedtls_set_delay_fptr set_delay; // Function pointer to set delay
    mbedtls_get_delay_fptr get_delay; // Function pointer to get delay
} DtlsSessionTimer_t;

typedef struct DtlsRetransmissionParams
{
    DtlsSessionTimer_t transmissionTimer;
    uint32_t dtlsSessionStartTime;
    uint32_t dtlsSessionSetupTime;
} DtlsRetransmission_t;

/**
 * @brief Parameters for the network context of the transport interface
 * implementation that uses mbedTLS and UDP sockets.
 */
typedef struct DtlsTransportParams
{
    Socket_t udpSocket;
    DtlsSSLContext_t dtlsSslContext;
    mbedtls_timing_delay_context mbedtlsTimer;
    OnTransportDtlsSendHook onDtlsSendHook;
    void * pOnDtlsSendCustomContext;
    OnTransportDtlsRecvHook onDtlsRecvHook;
    void * pOnDtlsRecvCustomContext;
} DtlsTransportParams_t;


/**
 * @brief Each compilation unit that consumes the NetworkContext must define it.
 * It should contain a single pointer as seen below whenever the header file
 * of this transport implementation is included to your project.
 *
 * @note When using multiple transports in the same compilation unit,
 *       define this pointer as void *.
 */
struct DtlsNetworkContext
{
    DtlsTransportParams_t * pParams;
};
typedef struct DtlsNetworkContext DtlsNetworkContext_t;


// DtlsKeyingMaterial is information extracted via https://tools.ietf.org/html/rfc5705
// also includes the use_srtp value from Handshake
typedef struct
{
    uint8_t clientWriteKey[MAX_SRTP_MASTER_KEY_LEN + MAX_SRTP_SALT_KEY_LEN];
    uint8_t serverWriteKey[MAX_SRTP_MASTER_KEY_LEN + MAX_SRTP_SALT_KEY_LEN];
    uint8_t key_length;

    KVS_SRTP_PROFILE srtpProfile;
} DtlsKeyingMaterial, * pDtlsKeyingMaterial_t;


/**
 * @brief Contains the credentials necessary for tls connection setup.
 */
typedef struct DtlsNetworkCredentials
{
    /**
     * @brief To use ALPN, set this to a NULL-terminated list of supported
     * protocols in decreasing order of preference.
     *
     * See [this link]
     * (https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/)
     * for more information.
     */
    const char ** pAlpnProtos;

    /**
     * @brief Disable server name indication (SNI) for a (D)TLS session.
     */
    int disableSni;

    const uint8_t * pRootCa;     /**< @brief String representing a trusted server root certificate. */
    size_t rootCaSize;          /**< @brief Size associated with #NetworkCredentials.pRootCa. */
    mbedtls_x509_crt * pClientCert;             /**< @brief Client certificate context. */
    mbedtls_pk_context * pPrivateKey;

    DtlsKeyingMaterial dtlsKeyingMaterial; /**< @brief derivated SRTP keys */
} DtlsNetworkCredentials_t;

typedef struct DtlsSession {
    DtlsNetworkCredentials_t credentials;

    /* The transport layer interface used by the HTTP Client library. */
    TransportInterface_t xTransportInterface;
    /* The network context for the transport layer interface. */

    DtlsNetworkContext_t xNetworkContext;
    DtlsTransportParams_t xDtlsTransportParams;
    DtlsNetworkCredentials_t xNetworkCredentials;
} DtlsSession_t;

/**
 * @brief DTLS Connect / Disconnect return status.
 */
typedef enum DtlsTransportStatus
{
    DTLS_TRANSPORT_SUCCESS = 0,         /**< Function successfully completed. */
    DTLS_TRANSPORT_INVALID_PARAMETER,   /**< At least one parameter was invalid. */
    DTLS_TRANSPORT_INSUFFICIENT_MEMORY, /**< Insufficient memory required to establish connection. */
    DTLS_TRANSPORT_INVALID_CREDENTIALS, /**< Provided credentials were invalid. */
    DTLS_TRANSPORT_HANDSHAKE_FAILED,    /**< Performing TLS handshake with server failed. */
    DTLS_TRANSPORT_INTERNAL_ERROR,      /**< A call to a system API resulted in an internal error. */
    DTLS_TRANSPORT_CONNECT_FAILURE      /**< Initial connection to the server failed. */
} DtlsTransportStatus_t;

/**
 * @brief Create a DTLS connection with sockets.
 *
 * @param[out] pNetworkContext Pointer to a network context to contain the
 * connected socket handle.
 * @param[in] pNetworkCredentials Credentials for the TLS connection.
 *
 * @return #DTLS_TRANSPORT_SUCCESS, #DTLS_TRANSPORT_INSUFFICIENT_MEMORY, #DTLS_TRANSPORT_INVALID_CREDENTIALS,
 * #DTLS_TRANSPORT_HANDSHAKE_FAILED, #DTLS_TRANSPORT_INTERNAL_ERROR, or #DTLS_TRANSPORT_CONNECT_FAILURE.
 */
DtlsTransportStatus_t
DTLS_Connect( DtlsNetworkContext_t * pNetworkContext,
              DtlsNetworkCredentials_t * pNetworkCredentials,
              const char * pHostName,
              uint16_t port );

/**
 * @brief Gracefully disconnect an established DTLS connection.
 *
 * @param[in] pNetworkContext Network context.
 */
void DTLS_Disconnect( DtlsNetworkContext_t * pNetworkContext );

/**
 * @brief Receives data from an established DTLS connection.
 *
 * @note This is the DTLS version of the transport interface's
 * #TransportRecv_t function.
 *
 * @param[in] pNetworkContext The Network context.
 * @param[out] pBuffer Buffer to receive bytes into.
 * @param[in] bytesToRecv Number of bytes to receive from the network.
 *
 * @return Number of bytes (> 0) received if successful;
 * 0 if the socket times out without reading any bytes;
 * negative value on error.
 */
int32_t DTLS_recv( DtlsNetworkContext_t * pNetworkContext,
                   void * pBuffer,
                   size_t bytesToRecv );

/**
 * @brief Sends data over an established DTLS connection.
 *
 * @note This is the DTLS version of the transport interface's
 * #TransportSend_t function.
 *
 * @param[in] pNetworkContext The network context.
 * @param[in] pBuffer Buffer containing the bytes to send.
 * @param[in] bytesToSend Number of bytes to send from the buffer.
 *
 * @return Number of bytes (> 0) sent on success;
 * 0 if the socket times out without sending any bytes;
 * else a negative value to represent error.
 */
int32_t DTLS_send( DtlsNetworkContext_t * pNetworkContext,
                   const void * pBuffer,
                   size_t bytesToSend );
/**
 * @brief Get the socket FD for this network context.
 *
 * @param[in] pNetworkContext The network context.
 *
 * @return The socket descriptor if value >= 0. It returns -1 when failure.
 */
int32_t DTLS_GetSocketFd( DtlsNetworkContext_t * pNetworkContext );

#ifdef MBEDTLS_DTLS_DEBUG_C

/**
 * @brief Write an MBedTLS Debug message to the LogDebug() function
 *
 * @param[in] sslContext Pointer of the SSL Context that is being used
 * @param[in] level The severity level of the debug message from MBedTLS
 * @param[in] file Name of the file that the debug message is from
 * @param[in] line The line number that the debug message is from
 * @param[in] str The full string debug message from MBedTLS
 *
 * @return void
 */
void dtls_mbedtls_string_printf( void * dtlsSslContext,
                                 int level,
                                 const char * file,
                                 int line,
                                 const char * str );
#endif /* MBEDTLS_DTLS_DEBUG_C */

/* DTLS*/

#define DTLS_RSA_F4 0x10001L

#define PRIVATE_KEY_PCS_PEM_SIZE  228

#define GENERATED_CERTIFICATE_MAX_SIZE 4096
#define GENERATED_CERTIFICATE_BITS 2048
#define DTLS_CERT_MIN_SERIAL_NUM_SIZE 8
#define DTLS_CERT_MAX_SERIAL_NUM_SIZE 20
#define GENERATED_CERTIFICATE_DAYS 365
#define DTLS_SECONDS_IN_A_DAY ( 86400 )
#define GENERATED_CERTIFICATE_NAME "KVS-WebRTC-Client"
#define KEYING_EXTRACTOR_LABEL "EXTRACTOR-dtls_srtp"

#define DEFAULT_TIME_UNIT_IN_NANOS 100
#define HUNDREDS_OF_NANOS_IN_A_MICROSECOND ( ( uint64_t )10 )
#define HUNDREDS_OF_NANOS_IN_A_MILLISECOND ( HUNDREDS_OF_NANOS_IN_A_MICROSECOND * ( ( uint64_t )1000 ) )
#define HUNDREDS_OF_NANOS_IN_A_SECOND ( HUNDREDS_OF_NANOS_IN_A_MILLISECOND * ( ( uint64_t )1000 ) )
#define HUNDREDS_OF_NANOS_IN_A_MINUTE ( HUNDREDS_OF_NANOS_IN_A_SECOND * ( ( uint64_t )60 ) )
#define HUNDREDS_OF_NANOS_IN_AN_HOUR ( HUNDREDS_OF_NANOS_IN_A_MINUTE * ( ( uint64_t )60 ) )
#define HUNDREDS_OF_NANOS_IN_A_DAY ( HUNDREDS_OF_NANOS_IN_AN_HOUR * 24LL )

#define STATUS_SUCCESS ( ( uint32_t )0x00000000 )

#define STATUS_BASE 0x00000000
#define STATUS_NULL_ARG STATUS_BASE + 0x00000001
#define STATUS_INVALID_ARG STATUS_BASE + 0x00000002
#define STATUS_NOT_ENOUGH_MEMORY STATUS_BASE + 0x00000004

/////////////////////////////////////////////////////
/// DTLS related status codes
/////////////////////////////////////////////////////

int32_t createCertificateAndKey( int32_t certificateBits,
                                 int generateRSACertificate,
                                 mbedtls_x509_crt * pCert,
                                 mbedtls_pk_context * pKey );

int32_t freeCertificateAndKey( mbedtls_x509_crt * pCert,
                               mbedtls_pk_context * pKey );

int32_t dtlsCreateCertificateFingerprint( const mbedtls_x509_crt * pCert,
                                          char * pBuff,
                                          const size_t bufLen );

int32_t dtlsSessionVerifyRemoteCertificateFingerprint( DtlsSSLContext_t * pSslContext,
                                                       char * pExpectedFingerprint,
                                                       const size_t fingerprintMaxLen );

int32_t dtlsSessionPopulateKeyingMaterial( DtlsSSLContext_t * pSslContext,
                                           pDtlsKeyingMaterial_t pDtlsKeyingMaterial );

int32_t dtlsCertificateDemToPem( const unsigned char * der_data,
                                 size_t der_len,
                                 unsigned char * pem_buf,
                                 size_t pem_buf_len,
                                 size_t * olen );

#endif /* ifndef TRANSPORT_DTLS_MBEDTLS_H */
