/*
 * FreeRTOS V202212.01
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 * @file sockets_wrapper.c
 * @brief FreeRTOS Sockets connect and disconnect wrapper implementation for LWIP.
 */

/* Include header that defines log levels. */
#include "logging.h"

/* Standard includes. */
#include <string.h>
#include <assert.h>
#include <stdlib.h>

/* FreeRTOS includes. */
//#include "FreeRTOS.h"

/* LWIP includes */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

/* UDP Sockets Wrapper include.*/
#include "udp_sockets_wrapper.h"

/* assert() using stuff in task.h */
//#include "task.h"

struct xSOCKET
{
    int xFd;
};

int UDP_Sockets_CreateAndAssign( Socket_t * pUdpSocket,
                                 int assignFd )
{
    int xRet = UDP_SOCKETS_ERRNO_NONE;

    if( pUdpSocket == NULL )
    {
        xRet = UDP_SOCKETS_ERRNO_EINVAL;
        LogError( ( "Invalid input, pUdpSocket: %p", pUdpSocket ) );
    }

    if( xRet == UDP_SOCKETS_ERRNO_NONE )
    {
        *pUdpSocket = malloc( sizeof( *pUdpSocket ) );
        if( *pUdpSocket == NULL )
        {
            LogError( ( "Failed to allow new socket context." ) );
            xRet = UDP_SOCKETS_ERRNO_ENOMEM;
        }
        else
        {
            ( *pUdpSocket )->xFd = assignFd;
        }
    }

    return xRet;
}

/**
 * @brief Establish a connection to server.
 *
 * @param[out] pUdpSocket The output parameter to return the created socket descriptor.
 * @param[in] pHostName Server hostname to connect to.
 * @param[in] pServerInfo Server port to connect to.
 * @param[in] receiveTimeoutMs Timeout (in milliseconds) for transport receive.
 * @param[in] sendTimeoutMs Timeout (in milliseconds) for transport send.
 *
 * @note A timeout of 0 means infinite timeout.
 *
 * @return Non-zero value on error, 0 on success.
 */
int UDP_Sockets_Connect( Socket_t * pUdpSocket,
                         const char * pHostName,
                         uint16_t port,
                         uint32_t receiveTimeoutMs,
                         uint32_t sendTimeoutMs )
{
    int xFd = -1;
    int xRet = UDP_SOCKETS_ERRNO_NONE;
    struct addrinfo xHints, * pxAddrList, * pxCur;
    char xPortStr[6];

    memset( &xHints, 0, sizeof( xHints ) );
    xHints.ai_family = AF_UNSPEC;
    xHints.ai_socktype = SOCK_DGRAM;
    xHints.ai_protocol = IPPROTO_UDP;
    snprintf( xPortStr, sizeof( xPortStr ), "%d", port );
    if( getaddrinfo( pHostName, xPortStr, &xHints, &pxAddrList ) != 0 )
    {
        LogError( ( "Failed to connect to server: DNS resolution failed: Hostname=%s.",
                    pHostName ) );
        return UDP_SOCKETS_ERRNO_ERROR;
    }

    /* Try the sockaddrs until a connection succeeds */
    xRet = UDP_SOCKETS_ERRNO_ERROR;
    for( pxCur = pxAddrList; pxCur != NULL; pxCur = pxCur->ai_next )
    {
        xFd = ( *pUdpSocket )->xFd;
        if( xFd < 0 )
        {
            LogError( ( "Failed to create new socket." ) );
            xRet = UDP_SOCKETS_ERRNO_ENOMEM;
            continue;
        }

        if( connect( xFd, pxCur->ai_addr, pxCur->ai_addrlen ) == 0 )
        {
            xRet = UDP_SOCKETS_ERRNO_NONE;
            LogInfo( ( "Established UDP connection with %s.", pHostName ) );
            break;
        }
        else
        {
            LogInfo( ( "Connecting failed with %s.", pHostName ) );
        }

        // close(xFd);
        xRet = UDP_SOCKETS_ERRNO_ERROR;
    }

    freeaddrinfo( pxAddrList );

    // if (xRet == UDP_SOCKETS_ERRNO_NONE)
    // {
    //     *pUdpSocket = pvPortMalloc(sizeof(*pUdpSocket));
    //     if (*pUdpSocket == NULL)
    //     {
    //         LogError(("Failed to allow new socket context."));
    //         (void)close(xFd);
    //         xRet = UDP_SOCKETS_ERRNO_ENOMEM;
    //     }
    //     else
    //     {
    //         (*pUdpSocket)->xFd = xFd;
    //     }
    // }

    // if (xRet == UDP_SOCKETS_ERRNO_NONE)
    // {
    //     setsockopt( xFd, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeoutMs, sizeof( receiveTimeoutMs ) );
    //     setsockopt( xFd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeoutMs, sizeof( sendTimeoutMs ) );
    // }

    return xRet;
}

/**
 * @brief End connection to server.
 *
 * @param[in] udpSocket The socket descriptor.
 */
void UDP_Sockets_Disconnect( Socket_t udpSocket )
{
    ( void )shutdown( udpSocket->xFd, SHUT_RDWR );
    free(udpSocket);
}

/**
 * @brief Transmit data to the remote socket.
 *
 * The socket must have already been created using a call to UDP_Sockets_Connect().
 *
 * @param[in] xSocket The handle of the sending socket.
 * @param[in] pvBuffer The buffer containing the data to be sent.
 * @param[in] xDataLength The length of the data to be sent.
 *
 * @return
 * * On success, the number of bytes actually sent is returned.
 * * If an error occurred, a negative value is returned. @ref SocketsErrors
 */
int32_t UDP_Sockets_Send( Socket_t xSocket,
                          const void * pvBuffer,
                          size_t xBufferLength )
{
    int xWriteRet;
    int xReturnStatus;

    assert( xSocket != NULL );
    assert( pvBuffer != NULL );

    xWriteRet = write( xSocket->xFd, pvBuffer, xBufferLength );
    if( xWriteRet >= 0 )
    {
        xReturnStatus = xWriteRet;
    }
    else
    {
        switch( errno )
        {
            case EPIPE:
            case ECONNRESET:
                xReturnStatus = UDP_SOCKETS_ERRNO_ENOTCONN;
                break;
            default:
                xReturnStatus = UDP_SOCKETS_ERRNO_ERROR;
                break;
        }
    }
    return xReturnStatus;
}

/**
 * @brief Receive data from a UDP socket.
 *
 * The socket must have already been created using a call to UDP_Sockets_Connect().
 *
 * @param[in] xSocket The handle of the socket from which data is being received.
 * @param[out] pvBuffer The buffer into which the received data will be placed.
 * @param[in] xBufferLength The maximum number of bytes which can be received.
 * pvBuffer must be at least xBufferLength bytes long.
 *
 * @return
 * * If the receive was successful then the number of bytes received (placed in the
 *   buffer pointed to by pvBuffer) is returned.
 * * If a timeout occurred before data could be received then 0 is returned (timeout
 *   is set using @ref SOCKETS_SO_RCVTIMEO).
 * * If an error occurred, a negative value is returned. @ref SocketsErrors
 */
int32_t UDP_Sockets_Recv( Socket_t xSocket,
                          void * pvBuffer,
                          size_t xBufferLength )
{
    int xReadRet;
    int xReturnStatus;

    assert( xSocket != NULL );
    assert( pvBuffer != NULL );

    xReadRet = read( xSocket->xFd, pvBuffer, xBufferLength );
    if( xReadRet >= 0 )
    {
        xReturnStatus = xReadRet;
    }
    else
    {
        switch( errno )
        {
            case EWOULDBLOCK:
            case EINTR:
                xReturnStatus = 0;
                break;
            case EPIPE:
            case ECONNRESET:
                xReturnStatus = UDP_SOCKETS_ERRNO_ENOTCONN;
                break;
            default:
                xReturnStatus = UDP_SOCKETS_ERRNO_ERROR;
                break;
        }
    }
    return xReturnStatus;
}

int32_t UDP_Sockets_GetSocketFd( Socket_t xSocket )
{
    int32_t ret = -1;

    if( xSocket != NULL )
    {
        ret = xSocket->xFd;
    }

    return ret;
}
