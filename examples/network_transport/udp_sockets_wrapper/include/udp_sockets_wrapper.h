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
 * @file udp_sockets_wrapper.h
 * @brief UDP transport functions wrapper.
 */

#ifndef UDP_SOCKETS_WRAPPER_H
#define UDP_SOCKETS_WRAPPER_H


/* Standard includes. */
#include <stdint.h>


/* Error codes. */
#define UDP_SOCKETS_ERRNO_NONE                ( 0 )   /*!< No error. */
#define UDP_SOCKETS_ERRNO_ERROR               ( -1 )  /*!< Catch-all sockets error code. */
#define UDP_SOCKETS_ERRNO_EWOULDBLOCK         ( -2 )  /*!< A resource is temporarily unavailable. */
#define UDP_SOCKETS_ERRNO_ENOMEM              ( -3 )  /*!< Memory allocation failed. */
#define UDP_SOCKETS_ERRNO_EINVAL              ( -4 )  /*!< Invalid argument. */
#define UDP_SOCKETS_ERRNO_ENOPROTOOPT         ( -5 )  /*!< A bad option was specified . */
#define UDP_SOCKETS_ERRNO_ENOTCONN            ( -6 )  /*!< The supplied socket is not connected. */
#define UDP_SOCKETS_ERRNO_EISCONN             ( -7 )  /*!< The supplied socket is already connected. */
#define UDP_SOCKETS_ERRNO_ECLOSED             ( -8 )  /*!< The supplied socket has already been closed. */
#define UDP_SOCKETS_ERRNO_PERIPHERAL_RESET    ( -9 )  /*!< Communications peripheral has been reset. */
#define UDP_SOCKETS_ERRNO_ENOSPC              ( -10 ) /*!< No space left on device */
#define UDP_SOCKETS_ERRNO_EINTR               ( -11 ) /*!< Interrupted system call */

#ifndef SOCKET_T_TYPEDEFED
    struct xSOCKET;
    typedef struct xSOCKET * Socket_t; /**< @brief Socket handle data type. */
#endif

/**
 * @brief Create an UDP socket descriptor.
 *
 * @param[out] pUdpSocket The output parameter to return the created socket descriptor.
 * @param[in] assignFd The existing socket descriptor to be assigned to pUdpSocket.
 *
 * @return Non-zero value on error, 0 on success.
 */
int UDP_Sockets_CreateAndAssign( Socket_t * pUdpSocket,
                                 int assignFd );

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
                         uint32_t sendTimeoutMs );

/**
 * @brief End connection to server.
 *
 * @param[in] udpSocket The socket descriptor.
 */
void UDP_Sockets_Disconnect( Socket_t udpSocket );

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
                          size_t xDataLength );

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
                          size_t xBufferLength );

/**
 * @brief Get socket descriptor from Socket_t structure.
 *
 * @param[in] xSocket The handle of the socket
 *
 * @return
 * * Return socket descriptor with value >= 0. Otherwise, it returns -1.
 */
int32_t UDP_Sockets_GetSocketFd( Socket_t xSocket );

#endif /* ifndef UDP_SOCKETS_WRAPPER_H */
