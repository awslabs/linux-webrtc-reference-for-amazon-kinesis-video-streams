#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
#include "ice_api.h"
#include "core_json.h"
#include "string_utils.h"

#define ICE_CONTROLLER_CANDIDATE_JSON_KEY "candidate"

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

static IceControllerResult_t initializeIceAgent( IceControllerRemoteInfo_t *RemoteInfo )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    uint32_t i;

    for( i=0 ; i<ICE_MAX_LOCAL_CANDIDATE_COUNT ; i++ )
    {
        RemoteInfo->iceAgent.localCandidates[i] = &RemoteInfo->localCandidates[i];
    }

    for( i=0 ; i<ICE_MAX_REMOTE_CANDIDATE_COUNT ; i++ )
    {
        RemoteInfo->iceAgent.remoteCandidates[i] = &RemoteInfo->remoteCandidates[i];
    }

    for( i=0 ; i<ICE_MAX_CANDIDATE_PAIR_COUNT ; i++ )
    {
        RemoteInfo->iceAgent.iceCandidatePairs[i] = &RemoteInfo->candidatePairs[i];
    }

    for( i=0 ; i<ICE_MAX_CANDIDATE_PAIR_COUNT ; i++ )
    {
        /* TODO: the buffer assignment might be changed later in ICE component. */
        RemoteInfo->iceAgent.stunMessageBuffers[i] = RemoteInfo->stunBuffers[i];
    }

    return ret;
}

IceControllerResult_t IceController_Init( IceControllerContext_t *pCtx, SignalingControllerContext_t *pSignalingControllerContext )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;

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
