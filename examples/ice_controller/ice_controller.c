#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
#include "ice_api.h"
#include "core_json.h"
#include "string_utils.h"

#define ICE_CONTROLLER_CANDIDATE_JSON_KEY "candidate"
#define MAX_QUEUE_MSG_NUM ( 10 )
#define POLL_TIMEOUT_MS 500

/* Generate a printable string that does not
 * need to be escaped when encoding in JSON
 */
static void generateJSONValidString( char *pDst, size_t length )
{
    size_t i = 0;
    uint8_t skipProcess = 0;
    const char jsonCharSet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
    const size_t jsonCharSetLength = strlen( jsonCharSet );

    if( pDst == NULL )
    {
        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        for( i = 0; i < length; i++ )
        {
            pDst[i] = jsonCharSet[ rand() % jsonCharSetLength ];
        }
    }
}

static IceControllerResult_t parseIceCandidate( const char *pDecodeMessage, size_t decodeMessageLength, const char **ppCandidateString, size_t *pCandidateStringLength )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    JSONStatus_t jsonResult;
    size_t start = 0, next = 0;
    JSONPair_t pair = { 0 };
    uint8_t isCandidateFound = 0;

    jsonResult = JSON_Validate( pDecodeMessage, decodeMessageLength );
    if( jsonResult != JSONSuccess)
    {
        ret = ICE_CONTROLLER_RESULT_INVALID_JSON;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Check if it's SDP offer. */
        jsonResult = JSON_Iterate( pDecodeMessage, decodeMessageLength, &start, &next, &pair );

        while( jsonResult == JSONSuccess )
        {
            if( pair.keyLength == strlen( ICE_CONTROLLER_CANDIDATE_JSON_KEY ) &&
                strncmp( pair.key, ICE_CONTROLLER_CANDIDATE_JSON_KEY, pair.keyLength ) == 0 )
            {
                *ppCandidateString = pair.value;
                *pCandidateStringLength = pair.valueLength;
                isCandidateFound = 1;

                break;
            }

            jsonResult = JSON_Iterate( pDecodeMessage, decodeMessageLength, &start, &next, &pair );
        }
    }

    if( isCandidateFound == 0 )
    {
        ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_NOT_FOUND;
    }

    return ret;
}

static IceControllerRemoteInfo_t *allocateRemoteInfo( IceControllerContext_t *pCtx )
{
    IceControllerRemoteInfo_t *pRet = NULL;
    int32_t i;

    for( i=0 ; i<AWS_MAX_VIEWER_NUM ; i++ )
    {
        if( pCtx->remoteInfo[i].isUsed == 0 )
        {
            pRet = &pCtx->remoteInfo[i];
            pRet->isUsed = 1;
            break;
        }
    }

    return pRet;
}

static void freeRemoteInfo( IceControllerContext_t *pCtx, int32_t index )
{
    pCtx->remoteInfo[index].isUsed = 0;
}

static IceControllerResult_t initializeIceAgent( IceControllerRemoteInfo_t *pRemoteInfo )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    uint32_t i;

    for( i=0 ; i<ICE_MAX_LOCAL_CANDIDATE_COUNT ; i++ )
    {
        pRemoteInfo->iceAgent.localCandidates[i] = &pRemoteInfo->localCandidates[i];
    }

    for( i=0 ; i<ICE_MAX_REMOTE_CANDIDATE_COUNT ; i++ )
    {
        pRemoteInfo->iceAgent.remoteCandidates[i] = &pRemoteInfo->remoteCandidates[i];
    }

    for( i=0 ; i<ICE_MAX_CANDIDATE_PAIR_COUNT ; i++ )
    {
        pRemoteInfo->iceAgent.iceCandidatePairs[i] = &pRemoteInfo->candidatePairs[i];
    }

    for( i=0 ; i<ICE_MAX_CANDIDATE_PAIR_COUNT ; i++ )
    {
        /* TODO: the buffer assignment might be changed later in ICE component. */
        pRemoteInfo->iceAgent.stunMessageBuffers[i] = pRemoteInfo->stunBuffers[i];
    }

    return ret;
}

static IceControllerResult_t findRemoteInfo( IceControllerContext_t *pCtx, const char *pRemoteClientId, size_t remoteClientIdLength, IceControllerRemoteInfo_t **ppRemoteInfo )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    size_t remoteInfoIndex;
    uint8_t isRemoteInfoFound=0;

    if( remoteClientIdLength > SIGNALING_CONTROLLER_REMOTE_ID_MAX_LENGTH )
    {
        ret = ICE_CONTROLLER_RESULT_INVALID_REMOTE_CLIENT_ID;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        for( remoteInfoIndex=0 ; remoteInfoIndex<AWS_MAX_VIEWER_NUM ; remoteInfoIndex++ )
        {
            if( strncmp( pCtx->remoteInfo[ remoteInfoIndex ].remoteClientId, pRemoteClientId, remoteClientIdLength ) == 0 )
            {
                isRemoteInfoFound = 1;
                *ppRemoteInfo = &pCtx->remoteInfo[ remoteInfoIndex ];
                break;
            }
        }

        if( !isRemoteInfoFound )
        {
            ret = ICE_CONTROLLER_RESULT_UNKNOWN_REMOTE_CLIENT_ID;
        }
    }

    return ret;
}

static IceControllerResult_t handleAddRemoteCandidateRequest( IceControllerContext_t *pCtx, IceControllerRequestMessage_t *pRequestMessage, size_t requestMessageLength )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceResult_t iceResult;
    IceControllerCandidate_t *pCandidate = (IceControllerCandidate_t *)&pRequestMessage->requestContent;
    IceControllerRemoteInfo_t *pRemoteInfo;
    IceCandidate_t *pOutCandidate;

    /* Find remote info index by mapping remote client ID. */
    ret = findRemoteInfo( pCtx, pCandidate->remoteClientId, pCandidate->remoteClientIdLength, &pRemoteInfo );

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        iceResult = Ice_AddRemoteCandidate( &pRemoteInfo->iceAgent,
                                            pCandidate->candidateType,
                                            &pOutCandidate,
                                            pCandidate->iceIpAddress,
                                            pCandidate->protocol,
                                            pCandidate->priority );
        if( iceResult != ICE_RESULT_OK )
        {
            LogError( ( "Fail to add remote candidate, result: %d", iceResult ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_ADD_REMOTE_CANDIDATE;
        }
    }

    return ret;
}

static IceControllerResult_t handleRequest( IceControllerContext_t *pCtx, MessageQueueHandler_t *pRequestQueue )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    MessageQueueResult_t retMessageQueue;
    IceControllerRequestMessage_t requestMsg;
    size_t requestMsgLength;

    retMessageQueue = MessageQueue_IsEmpty( pRequestQueue );
    if( retMessageQueue == MESSAGE_QUEUE_RESULT_MQ_HAVE_MESSAGE )
    {
        /* Handle event. */
        requestMsgLength = sizeof( IceControllerRequestMessage_t );
        retMessageQueue = MessageQueue_Recv( pRequestQueue, &requestMsg, &requestMsgLength );
        if( retMessageQueue == MESSAGE_QUEUE_RESULT_OK )
        {
            /* Received message, process it. */
            LogDebug( ( "Receive request type: %d", requestMsg.requestType ) );
            switch( requestMsg.requestType )
            {
                case ICE_CONTROLLER_REQUEST_TYPE_ADD_REMOTE_CANDIDATE:
                    ret = handleAddRemoteCandidateRequest( pCtx, &requestMsg, requestMsgLength );
                    break;
                default:
                    /* Unknown request, drop it. */
                    LogDebug( ( "Dropping unknown request" ) );
                    break;
            }
        }
    }

    return ret;
}

IceControllerResult_t IceController_Deinit( IceControllerContext_t *pCtx )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

    if( pCtx == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Free mqueue. */
        MessageQueue_Destroy( &pCtx->requestQueue );
    }

    return ret;
}

IceControllerResult_t IceController_Init( IceControllerContext_t *pCtx, SignalingControllerContext_t *pSignalingControllerContext )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    MessageQueueResult_t retMessageQueue;

    if( pCtx == NULL || pSignalingControllerContext == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        memset( pCtx, 0, sizeof( IceControllerContext_t ) );

        /* Generate local name/password. */
        generateJSONValidString( pCtx->localUserName, ICE_CONTROLLER_USER_NAME_LENGTH );
        pCtx->localUserName[ ICE_CONTROLLER_USER_NAME_LENGTH ] = '\0';
        generateJSONValidString( pCtx->localPassword, ICE_CONTROLLER_PASSWORD_LENGTH );
        pCtx->localPassword[ ICE_CONTROLLER_PASSWORD_LENGTH ] = '\0';

        pCtx->pSignalingControllerContext = pSignalingControllerContext;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        retMessageQueue = MessageQueue_Create( &pCtx->requestQueue, "/IceController", sizeof( IceControllerRequestMessage_t ), MAX_QUEUE_MSG_NUM );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to open message queue, errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_MQ_INIT;
        }
    }

    return ret;
}

IceControllerResult_t IceController_DeserializeIceCandidate( const char *pDecodeMessage, size_t decodeMessageLength, IceControllerCandidate_t *pCandidate )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    StringUtilsResult_t stringResult;
    const char *pCandidateString;
    size_t candidateStringLength;
    const char *pCurr, *pTail, *pNext;
    size_t tokenLength;
    IceControllerCandidateDeserializerState_t deserializerState = ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_FOUNDATION;
    uint8_t isAllElementsParsed = 0;

    if( pDecodeMessage == NULL || pCandidate == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* parse json message and get the candidate string. */
        ret = parseIceCandidate( pDecodeMessage, decodeMessageLength, &pCandidateString, &candidateStringLength );

        pCurr = pCandidateString;
        pTail = pCandidateString + candidateStringLength;
    }

    /* deserialize candidate string into structure. */
    while( ret == ICE_CONTROLLER_RESULT_OK &&
           ( pNext = memchr( pCurr, ' ', pTail - pCurr ) ) != NULL &&
           deserializerState <= ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_MAX )
    {
        tokenLength = pNext - pCurr;

        switch( deserializerState )
        {
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_FOUNDATION:
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_COMPONENT:
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PROTOCOL:
                if( strncmp( pCurr, "tcp", tokenLength ) == 0 )
                {
                    pCandidate->protocol = ICE_SOCKET_PROTOCOL_TCP;
                }
                else if( strncmp( pCurr, "udp", tokenLength ) == 0 )
                {
                    pCandidate->protocol = ICE_SOCKET_PROTOCOL_UDP;
                }
                else
                {
                    LogWarn( ( "unknown protocol %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PROTOCOL;
                }
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PRIORITY:
                stringResult = StringUtils_ConvertStringToUl( pCurr, tokenLength, &pCandidate->priority );
                if( stringResult != STRING_UTILS_RESULT_OK )
                {
                    LogWarn( ( "Invalid priority %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PRIORITY;
                }
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_IP:
                ret = IceControllerNet_ConvertIpString( pCurr, tokenLength, &pCandidate->iceIpAddress );
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_PORT:
                stringResult = StringUtils_ConvertStringToUl( pCurr, tokenLength, (uint32_t *) &pCandidate->port );
                if( stringResult != STRING_UTILS_RESULT_OK )
                {
                    LogWarn( ( "Invalid port %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PORT;
                }
                else
                {
                    /* Also update port in address following network endianness. */
                    ret = IceControllerNet_Htons( pCandidate->port, &pCandidate->iceIpAddress.ipAddress.port );
                }
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_TYPE_ID:
                if( tokenLength != strlen( "typ" ) || strncmp( pCurr, "typ", tokenLength ) != 0 )
                {
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_TYPE_ID;
                }
                break;
            case ICE_CONTROLLER_CANDIDATE_DESERIALIZER_STATE_TYPE_VAL:
                isAllElementsParsed = 1;

                if( strncmp( pCurr, ICE_CONTROLLER_CANDIDATE_TYPE_HOST_STRING, tokenLength ) == 0 )
                {
                    pCandidate->candidateType = ICE_CANDIDATE_TYPE_HOST;
                }
                else if( strncmp( pCurr, ICE_CONTROLLER_CANDIDATE_TYPE_SRFLX_STRING, tokenLength ) == 0 )
                {
                    pCandidate->candidateType = ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
                }
                else if( strncmp( pCurr, ICE_CONTROLLER_CANDIDATE_TYPE_PRFLX_STRING, tokenLength ) == 0 )
                {
                    pCandidate->candidateType = ICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
                }
                else if( strncmp( pCurr, ICE_CONTROLLER_CANDIDATE_TYPE_RELAY_STRING, tokenLength ) == 0 )
                {
                    pCandidate->candidateType = ICE_CANDIDATE_TYPE_RELAYED;
                }
                else
                {
                    LogWarn( ( "unknown candidate type %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_TYPE;
                }
                break;
            default:
                break;
        }

        pCurr = pNext + 1;
        deserializerState++;
    }

    if( isAllElementsParsed != 1 )
    {
        ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_LACK_OF_ELEMENT;
    }

    return ret;
}

IceControllerResult_t IceController_SetRemoteDescription( IceControllerContext_t *pCtx, const char *pRemoteClientId, size_t remoteClientIdLength, const char *pRemoteUserName, size_t remoteUserNameLength, const char *pRemotePassword, size_t remotePasswordLength )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceResult_t iceResult;
    char combinedName[ MAX_ICE_CONFIG_USER_NAME_LEN + 1 ];
    IceControllerRemoteInfo_t *pRemoteInfo;

    if( pCtx == NULL || pRemoteClientId == NULL ||
        pRemoteUserName == NULL || pRemotePassword == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        pRemoteInfo = allocateRemoteInfo( pCtx );
        if( pRemoteInfo == NULL )
        {
            LogWarn( ( "Fail to allocate remote info" ) );
            ret = ICE_CONTROLLER_RESULT_EXCEED_REMOTE_PEER;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Store remote client ID into context. */
        if( remoteClientIdLength > SIGNALING_CONTROLLER_REMOTE_ID_MAX_LENGTH )
        {
            LogWarn( ( "Remote ID is too long to store, length: %ld", remoteClientIdLength ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_REMOTE_CLIENT_ID;
        }
        else
        {
            memcpy( pRemoteInfo->remoteClientId, pRemoteClientId, remoteClientIdLength );
            pRemoteInfo->remoteClientIdLength = remoteClientIdLength;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Prepare combine name and create Ice Agent. */
        if( remoteUserNameLength + ICE_CONTROLLER_USER_NAME_LENGTH > MAX_ICE_CONFIG_USER_NAME_LEN )
        {
            LogWarn( ( "Remote user name is too long to store, length: %ld", remoteUserNameLength ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_REMOTE_USERNAME;
        }
        else
        {
            memcpy( combinedName, pCtx->localUserName, ICE_CONTROLLER_USER_NAME_LENGTH );
            memcpy( combinedName + ICE_CONTROLLER_USER_NAME_LENGTH, pRemoteUserName, remoteUserNameLength );
            combinedName[ remoteUserNameLength + ICE_CONTROLLER_USER_NAME_LENGTH ] = '\0';

            iceResult = Ice_CreateIceAgent( &pRemoteInfo->iceAgent,
                                            pCtx->localUserName, pCtx->localPassword,
                                            ( char* ) pRemoteUserName, ( char* ) pRemotePassword,
                                            combinedName,
                                            &pRemoteInfo->transactionIdStore );
            if( iceResult != ICE_RESULT_OK )
            {
                LogError( ( "Fail to create ICE agent, result: %d", iceResult ) );
                ret = ICE_CONTROLLER_RESULT_FAIL_CREATE_ICE_AGENT;
            }
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Initialize candidate pointers in Ice Agent. */
        ret = initializeIceAgent( pRemoteInfo );
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        ret = IceControllerNet_AddHostCandidates( pCtx, pRemoteInfo );
    }

    return ret;
}

IceControllerResult_t IceController_SendRemoteCandidateRequest( IceControllerContext_t *pCtx, const char *pRemoteClientId, size_t remoteClientIdLength, IceControllerCandidate_t *pCandidate )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    MessageQueueResult_t retMessageQueue;
    IceControllerRequestMessage_t requestMessage = {
        .requestType = ICE_CONTROLLER_REQUEST_TYPE_ADD_REMOTE_CANDIDATE,
    };
    IceControllerCandidate_t *pMessageContent;

    if( pCtx == NULL || pCandidate == NULL || pRemoteClientId == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( remoteClientIdLength > SIGNALING_CONTROLLER_REMOTE_ID_MAX_LENGTH )
    {
        ret = ICE_CONTROLLER_RESULT_INVALID_REMOTE_CLIENT_ID;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        pMessageContent = &requestMessage.requestContent.remoteCandidate;
        memcpy( pMessageContent, pCandidate, sizeof( IceControllerCandidate_t ) );
        memcpy( pMessageContent->remoteClientId, pRemoteClientId, remoteClientIdLength );
        pMessageContent->remoteClientIdLength = remoteClientIdLength;

        retMessageQueue = MessageQueue_Send( &pCtx->requestQueue, &requestMessage, sizeof( IceControllerRequestMessage_t ) );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            ret = ICE_CONTROLLER_RESULT_FAIL_MQ_SEND;
        }
    }

    return ret;
}

IceControllerResult_t IceController_ProcessLoop( IceControllerContext_t *pCtx )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    MessageQueueResult_t retMessageQueue;
    size_t i, remoteInfoIndex, fdsCount;
    int pollResult;

    if( pCtx == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* We leverage poll() to monitor bother message queue, request, and socket data together. */
        for( ;; )
        {
            fdsCount = 0;

            retMessageQueue = MessageQueue_AttachPoll( &pCtx->requestQueue, &pCtx->fds[ fdsCount++ ], POLLIN );
            if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
            {
                LogError( ( "MessageQueue_AttachPoll return fail, result: %d", retMessageQueue ) );
                break;
            }

            // for( remoteInfoIndex=0 ; remoteInfoIndex<AWS_MAX_VIEWER_NUM ; remoteInfoIndex++ )
            // {
            //     for( i=0 ; i < pCtx->remoteInfo[ remoteInfoIndex ].socketsFdLocalCandidatesCount ; i++ )
            //     {
            //         /* Attach local sockets. */
            //         pCtx->fds[ fdsCount ].fd = pCtx->remoteInfo[ remoteInfoIndex ].socketsFdLocalCandidates[ i ];
            //         pCtx->fds[ fdsCount++ ].events = POLLIN;
            //     }
            // }

            // Poll the message queue descriptor
            pollResult = poll( pCtx->fds, fdsCount, POLL_TIMEOUT_MS ); // Wait indefinitely
            if( pollResult < 0 )
            {
                LogError( ( "poll fails , errno: %s", strerror( errno ) ) );
                ret = ICE_CONTROLLER_RESULT_FAIL_POLLING;
                break;
            }
            else if( pollResult == 0 )
            {
                /* timeout, skip this round. */
                continue;
            }
            else
            {
                /* Do nothing, coverity happy. */
            }

            /* TODO Handle receiving events socket by socket. */
            // for( remoteInfoIndex=0 ; remoteInfoIndex<AWS_MAX_VIEWER_NUM ; remoteInfoIndex++ )
            // {
            //     for( i=0 ; i < pCtx->remoteInfo[ remoteInfoIndex ].socketsFdLocalCandidatesCount ; i++ )
            //     {
            //         if( fds[ remoteInfoIndex + i ].revents & POLLIN )
            //         {
            //             /* Receive socket data, handle it. */
            //         }
            //     }
            // }

            /* Handle message queue. */
            if( pCtx->fds[ 0 ].revents & POLLIN )
            {
                ret = handleRequest( pCtx, &pCtx->requestQueue );
                if( ret != ICE_CONTROLLER_RESULT_OK )
                {
                    break;
                }
            }
        }
    }

    return ret;
}
