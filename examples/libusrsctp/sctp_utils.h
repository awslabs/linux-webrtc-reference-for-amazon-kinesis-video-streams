#ifndef DATA_CHANNEL_SCTP_H
#define DATA_CHANNEL_SCTP_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define INET  1
#define INET6 1
#include <usrsctp.h>

#include "dcep_api.h"

#define SCTP_STATUS_ERR_FAIL 1U

/* 1200 - 12 (SCTP header Size) */
#define SCTP_MTU                         1188
#define SCTP_ASSOCIATION_DEFAULT_PORT    5000
#define SCTP_MAX_ALLOWABLE_PACKET_LENGTH ( DCEP_HEADER_LENGTH + MAX_DATA_CHANNEL_NAME_LEN + MAX_DATA_CHANNEL_PROTOCOL_LEN + 2 )

#define SCTP_SESSION_ACTIVE             0
#define SCTP_SESSION_SHUTDOWN_INITIATED 1
#define SCTP_SESSION_SHUTDOWN_COMPLETED 2

#define MAX_DATA_CHANNEL_NAME_LEN       255

#define MAX_DATA_CHANNEL_PROTOCOL_LEN   255

#define DEFAULT_SCTP_SHUTDOWN_TIMEOUT_SECONDS   ( 2 )
#define SECONDS_TO_USEC( x ) ( ( x ) * 1000000 )
#define DEFAULT_USRSCTP_TEARDOWN_POLLING_INTERVAL_USEC ( 10 )

enum { SCTP_PPID_DCEP = 50, SCTP_PPID_STRING = 51, SCTP_PPID_BINARY = 53, SCTP_PPID_STRING_EMPTY = 56, SCTP_PPID_BINARY_EMPTY = 57 };

typedef enum SctpUtilsResult
{
    SCTP_UTILS_RESULT_OK = 0,
    SCTP_UTILS_RESULT_FAIL,
    SCTP_UTILS_RESULT_FAIL_BAD_PARAMETER,
    SCTP_UTILS_RESULT_FAIL_DCEP_LIB_FAIL,
    SCTP_UTILS_RESULT_FAIL_SET_SOCKET_OPTIONS,
    SCTP_UTILS_RESULT_FAIL_INVALID_DCEP_PACKET,
    SCTP_UTILS_RESULT_FAIL_CLOSE_DATA_CHANNEL,
    SCTP_UTILS_RESULT_FAIL_SCTP_SEND_FAIL,
} SctpUtilsResult_t;

/* Callback that is fired when SCTP Association wishes to send packet */
typedef void (* SCTPSessionOutboundPacket_t)( void *,
                                              uint8_t *,
                                              uint32_t );

/* Callback that is fired when SCTP has a valid DATA_CHANNEL_OPEN Message
 * Argument is ChannelID and ChannelName + Len
 */
typedef void (* SCTPSessionDataChannelOpen_t)( void *,
                                               uint32_t,
                                               const uint8_t *,
                                               uint32_t );

/* Callback that is fired when SCTP has a received a DATA_CHANNEL_ACK Message
 * Argument is ChannelID and ChannelName + Len
 */
typedef SctpUtilsResult_t (* SCTPSessionDataChannelAck_t)( void *,
                                                           uint32_t );

/* Callback that is fired when SCTP has a DataChannel Message.
 * Argument is ChannelID and Message + Len
 */
typedef void (* SCTPSessionDataChannelMessage_t)( void *,
                                                  uint32_t,
                                                  uint8_t,
                                                  uint8_t *,
                                                  uint32_t );

typedef void (* RtcOnMessage)( void *,
                               uint32_t,
                               uint8_t,
                               uint8_t *,
                               uint32_t );

typedef void (* RtcOnOpen)( void *,
                            uint32_t );


typedef struct {
    void * customData;
    SCTPSessionOutboundPacket_t outboundPacketFunc;
    SCTPSessionDataChannelOpen_t dataChannelOpenFunc;
    SCTPSessionDataChannelAck_t dataChannelOpenAckFunc;
    SCTPSessionDataChannelMessage_t dataChannelMessageFunc;
} SCTPSessionCallbacks_t;

typedef struct {
    volatile size_t shutdownStatus;
    struct socket * socket;
    struct sctp_sendv_spa spa;
    uint8_t packet[SCTP_MAX_ALLOWABLE_PACKET_LENGTH];
    size_t packetSize;
    SCTPSessionCallbacks_t sctpSessionCallbacks;
    uint32_t ulCurrentDataChannelId;
} SCTPSession_t;

typedef struct {
    uint8_t isNull;  /* If this value is set, the value field will be ignored */
    uint16_t value;  /* This value is used only if isNull is not set.         */
                     /* Can be set to a unsigned 16 bit value                 */
} NullableUint16_t;

typedef struct {
    uint8_t ordered;                                    /* Decides the order in which data is sent. If true, data is sent in order            */
    NullableUint16_t maxPacketLifeTime;                 /* Limits the time (in milliseconds) during which the channel will (re)transmit       */
                                                        /* data if not acknowledged. This value may be clamped if it exceeds the maximum      */
                                                        /* value supported by the user agent.                                                 */
    NullableUint16_t maxRetransmits;                    /* Control number of times a channel retransmits data if not delivered successfully   */
    char protocol[MAX_DATA_CHANNEL_PROTOCOL_LEN + 1];   /* Sub protocol name for the channel                                                  */
    uint8_t negotiated;                                 /* If set to true, it is up to the application to negotiate the channel and create an */
                                                        /* RTCDataChannel object with the same id as the other peer.                          */
} DataChannelInit_t;

SctpUtilsResult_t SCTP_InitSCTPSession( void );
void SCTP_DeInitSCTPSession( void );
SctpUtilsResult_t SCTP_CreateSCTPSession( SCTPSession_t * pSctpSession );
SctpUtilsResult_t SCTP_FreeSCTPSession( SCTPSession_t * pSctpSession );
SctpUtilsResult_t SCTP_PutSCTPPacket( SCTPSession_t *,
                                      uint8_t *,
                                      uint32_t );
SctpUtilsResult_t SCTP_WriteMessageSCTPSession( SCTPSession_t *,
                                                uint32_t,
                                                uint8_t,
                                                uint8_t *,
                                                uint32_t );
SctpUtilsResult_t SCTP_SendDcepOpenDataChannel( SCTPSession_t *,
                                                uint32_t,
                                                char *,
                                                uint32_t,
                                                DataChannelInit_t * );
SctpUtilsResult_t SCTP_StreamReset( SCTPSession_t *,
                                    uint32_t );

#ifdef __cplusplus
}
#endif
#endif /* DATA_CHANNEL_SCTP_H */
