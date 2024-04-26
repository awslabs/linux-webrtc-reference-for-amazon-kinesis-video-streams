#ifndef ICE_CONTROLLER_DATA_TYPES_H
#define ICE_CONTROLLER_DATA_TYPES_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Standard includes. */
#include <stdint.h>
#include "demo_config.h"
#include "ice_data_types.h"
#include "signaling_controller_data_types.h"

// #define KVS_IP_ADDRESS_STRING_BUFFER_LEN ( 46 )
#define ICE_CONTROLLER_IP_ADDR_STRING_BUFFER_LENGTH ( 39 )
#define ICE_CONTROLLER_USER_NAME_LENGTH ( 4 )
#define ICE_CONTROLLER_PASSWORD_LENGTH ( 24 )
#define ICE_CONTROLLER_STUN_MESSAGE_BUFFER_SIZE ( 1024 )

typedef enum IceControllerResult
{
    ICE_CONTROLLER_RESULT_OK = 0,
    ICE_CONTROLLER_RESULT_BAD_PARAMETER,
    ICE_CONTROLLER_RESULT_IPV6_NOT_SUPPORT,
    ICE_CONTROLLER_RESULT_IP_BUFFER_TOO_SMALL,
    ICE_CONTROLLER_RESULT_RFDS_TOO_SMALL,
    ICE_CONTROLLER_RESULT_CANDIDATE_BUFFER_TOO_SMALL,
    ICE_CONTROLLER_RESULT_CANDIDATE_STRING_BUFFER_TOO_SMALL,
    ICE_CONTROLLER_RESULT_CANDIDATE_SEND_FAIL,
    ICE_CONTROLLER_RESULT_INVALID_IP_ADDR,
    ICE_CONTROLLER_RESULT_INVALID_JSON,
    ICE_CONTROLLER_RESULT_INVALID_REMOTE_CLIENT_ID,
    ICE_CONTROLLER_RESULT_INVALID_REMOTE_USERNAME,
    ICE_CONTROLLER_RESULT_FAIL_CREATE_ICE_AGENT,
    ICE_CONTROLLER_RESULT_FAIL_SOCKET_CREATE,
    ICE_CONTROLLER_RESULT_FAIL_SOCKET_BIND,
    ICE_CONTROLLER_RESULT_FAIL_SOCKET_GETSOCKNAME,
    ICE_CONTROLLER_RESULT_FAIL_ADD_HOST_CANDIDATE,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_NOT_FOUND,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PRIORITY,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PROTOCOL,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PORT,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_TYPE_ID,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_TYPE,
    ICE_CONTROLLER_RESULT_JSON_CANDIDATE_LACK_OF_ELEMENT,
    ICE_CONTROLLER_RESULT_EXCEED_REMOTE_PEER,
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

typedef struct IceControllerSignalingRemoteInfo
{
    /* Remote client ID is used to provide the destination of Signaling message. */
    uint8_t isUsed;
    char remoteClientId[ SIGNALING_CONTROLLER_REMOTE_ID_MAX_LENGTH ];
    size_t remoteClientIdLength;

    /* For ICE component. */
    IceAgent_t iceAgent;
    IceCandidate_t localCandidates[ ICE_MAX_LOCAL_CANDIDATE_COUNT ];
    IceCandidate_t remoteCandidates[ ICE_MAX_REMOTE_CANDIDATE_COUNT ];
    IceCandidatePair_t candidatePairs[ ICE_MAX_CANDIDATE_PAIR_COUNT ];
    uint8_t stunBuffers[ ICE_MAX_CANDIDATE_PAIR_COUNT ][ ICE_CONTROLLER_STUN_MESSAGE_BUFFER_SIZE ];
    TransactionIdStore_t transactionIdStore;
} IceControllerRemoteInfo_t;

typedef struct IceControllerContext
{
    /* The signaling controller context initialized by application. */
    SignalingControllerContext_t *pSignalingControllerContext;

    char localUserName[ ICE_CONTROLLER_USER_NAME_LENGTH + 1 ];
    char localPassword[ ICE_CONTROLLER_PASSWORD_LENGTH + 1 ];

    IceControllerRemoteInfo_t remoteInfo[ AWS_MAX_VIEWER_NUM ];
    IceIPAddress_t localIpAddresses[ ICE_MAX_LOCAL_CANDIDATE_COUNT ];
    size_t localIpAddressesCount;
    int socketsFdLocalCandidates[ ICE_MAX_LOCAL_CANDIDATE_COUNT ];
    size_t socketsFdLocalCandidatesCount;
    struct pollfd rfds[ ICE_MAX_LOCAL_CANDIDATE_COUNT ];
    size_t rfdsCount;
    size_t candidateFoundationCounter;
} IceControllerContext_t;

#ifdef __cplusplus
}
#endif

#endif /* ICE_CONTROLLER_DATA_TYPES_H */
