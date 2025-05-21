/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>

//#include "threading_alt.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ssl.h"

/* TCP Sockets Wrapper include.*/
#include "tcp_sockets_wrapper.h"

/* MbedTLS Bio TCP sockets wrapper include. */
#include "mbedtls_bio_tcp_sockets_wrapper.h"

/**
 * @brief Sends data over TCP socket.
 *
 * @param[in] ctx The network context containing the socket handle.
 * @param[in] buf Buffer containing the bytes to send.
 * @param[in] len Number of bytes to send from the buffer.
 *
 * @return Number of bytes sent on success; else a negative value.
 */
int xMbedTLSBioTCPSocketsWrapperSend( void * ctx,
                                      const unsigned char * buf,
                                      size_t len )
{
    int32_t xReturnStatus;

    assert( ctx != NULL );
    assert( buf != NULL );

    xReturnStatus = TCP_Sockets_Send( ( Socket_t ) ctx, buf, len );

    switch( xReturnStatus )
    {
        /* The send might be failed because of out-of-memory/interrupted by system call/EAGAIN. */
        case TCP_SOCKETS_ERRNO_EWOULDBLOCK:
            xReturnStatus = MBEDTLS_ERR_SSL_WANT_WRITE;
            break;
        /* Socket was closed or just got closed. */
        case TCP_SOCKETS_ERRNO_ENOTCONN:
        /* Not enough memory for the socket to create either an Rx or Tx stream. */
        case TCP_SOCKETS_ERRNO_ENOMEM:
        /* Socket is not valid, is not a TCP socket, or is not bound. */
        case TCP_SOCKETS_ERRNO_EINVAL:
        /* Socket received a signal, causing the read operation to be aborted. */
        case TCP_SOCKETS_ERRNO_EINTR:
            xReturnStatus = MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            break;

        /* A timeout occurred before any data could be sent. */
        case TCP_SOCKETS_ERRNO_ENOSPC:
            xReturnStatus = MBEDTLS_ERR_SSL_TIMEOUT;
            break;

        default:
            break;
    }

    return ( int ) xReturnStatus;
}

/**
 * @brief Receives data from TCP socket.
 *
 * @param[in] ctx The network context containing the socket handle.
 * @param[out] buf Buffer to receive bytes into.
 * @param[in] len Number of bytes to receive from the network.
 *
 * @return Number of bytes received if successful; Negative value on error.
 */
int xMbedTLSBioTCPSocketsWrapperRecv( void * ctx,
                                      unsigned char * buf,
                                      size_t len )
{
    int32_t xReturnStatus;

    assert( ctx != NULL );
    assert( buf != NULL );

    xReturnStatus = TCP_Sockets_Recv( ( Socket_t ) ctx, buf, len );

    switch( xReturnStatus )
    {
        /* No data could be sent because the socket was or just got closed. */
        case TCP_SOCKETS_ERRNO_ENOTCONN:
        /* No data could be sent because there was insufficient memory. */
        case TCP_SOCKETS_ERRNO_ENOMEM:
        /* No data could be sent because xSocket was not a valid TCP socket. */
        case TCP_SOCKETS_ERRNO_EINVAL:
            xReturnStatus = MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            break;

        /* A timeout occurred before any data could be received. */
        case 0:
            xReturnStatus = MBEDTLS_ERR_SSL_WANT_READ;
            break;

        default:
            break;
    }

    return ( int ) xReturnStatus;
}
