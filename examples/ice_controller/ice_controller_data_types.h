#ifndef ICE_CONTROLLER_DATA_TYPES_H
#define ICE_CONTROLLER_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Standard includes. */
#include <stdint.h>
#include "ice_data_types.h"

// #define KVS_IP_ADDRESS_STRING_BUFFER_LEN ( 46 )
#define ICE_CONTROLLER_IP_ADDR_STRING_BUFFER_LENGTH ( 39 )
#define ICE_CONTROLLER_USER_NAME_LENGTH ( 4 )
#define ICE_CONTROLLER_PASSWORD_LENGTH ( 24 )

typedef enum IceControllerResult
{
    ICE_CONTROLLER_RESULT_OK = 0,
    ICE_CONTROLLER_RESULT_BAD_PARAMETER,
    ICE_CONTROLLER_RESULT_IP_BUFFER_TOO_SMALL,
    ICE_CONTROLLER_RESULT_INVALID_IP_ADDR,
    ICE_CONTROLLER_RESULT_INVALID_JSON,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_NOT_FOUND,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PRIORITY,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PROTOCOL,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PORT,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_TYPE_ID,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_TYPE,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_LACK_OF_ELEMENT,
} IceControllerResult_t;

/* https://developer.mozilla.org/en-US/docs/Web/API/RTCIceCandidate/candidate
 * https://tools.ietf.org/html/rfc5245#section-15.1 */
typedef enum IceControllerCandidateDeserializerState
{
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_FOUNDATION = 0,
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_COMPONENT,
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PROTOCOL,
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PRIORITY,
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_IP,
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PORT,
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_TYPE_ID,
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_TYPE_VAL,
    ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_MAX,
} IceControllerCandidateDeserializerState_t;

typedef struct IceControllerCandidate
{
    IceSocketProtocol_t protocol;
    uint32_t priority;
    IceIPAddress_t iceIpAddress;
    uint16_t port;
    IceCandidateType_t candidateType;
} IceControllerCandidate_t;

typedef struct IceControllerContext
{
    char localUserName[ ICE_CONTROLLER_USER_NAME_LENGTH + 1 ];
    char localPassword[ ICE_CONTROLLER_PASSWORD_LENGTH + 1 ];

    // IceIPAddress_t destIp[ 10 ];
    // int destIpLen;
    // int sockets[ 10 ];
    // int socketUsed;
    // IceCandidate_t localCandidates[ ICE_MAX_LOCAL_CANDIDATE_COUNT ];
    // IceCandidate_t remoteCandidates[ ICE_MAX_REMOTE_CANDIDATE_COUNT ];
    // IceCandidatePair_t candidatePairs[ ICE_MAX_CANDIDATE_PAIR_COUNT ];
    // uint8_t stunBuffers[ ICE_MAX_CANDIDATE_PAIR_COUNT ];
    // IceAgent_t IceAgent;
    // TransactionIdStore_t TransactionIdStore;
    // struct pollfd rfds[ 101 ];
    // int nfds;
} IceControllerContext_t;

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_DATA_TYPES_H */
