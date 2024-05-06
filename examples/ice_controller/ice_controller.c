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
#include "signaling_controller.h"

#define ICE_CONTROLLER_CANDIDATE_JSON_KEY "candidate"
#define MAX_QUEUE_MSG_NUM ( 10 )
#define POLL_TIMEOUT_MS 500
#define REQUEST_QUEUE_POLL_ID ( 0 )
#define ICE_SERVER_TYPE_STUN "stun:"
#define ICE_SERVER_TYPE_STUN_LENGTH ( 5 )
#define ICE_SERVER_TYPE_TURN "turn:"
#define ICE_SERVER_TYPE_TURN_LENGTH ( 5 )
#define ICE_SERVER_TYPE_TURNS "turns:"
#define ICE_SERVER_TYPE_TURNS_LENGTH ( 6 )
#define ICE_CONTROLLER_CONNECTIVITY_TIMER_INTERVAL_MS ( 5000 )

static IceControllerResult_t IceController_SendConnectivityCheckRequest( IceControllerContext_t *pCtx, IceControllerRemoteInfo_t *pRemoteInfo );

static void onConnectivityCheckTimerExpire( void *pContext )
{
    IceControllerContext_t *pCtx = ( IceControllerContext_t * ) pContext;
    uint32_t i;
    IceControllerRequestMessage_t requestMsg;

    for( i = 0 ; i < AWS_MAX_VIEWER_NUM ; i++ )
    {
        if( pCtx->remoteInfo[ i ].isUsed )
        {
            ( void ) IceController_SendConnectivityCheckRequest( pCtx, &pCtx->remoteInfo[ i ] );
        }
    }
}

static IceControllerResult_t IceController_SendConnectivityCheckRequest( IceControllerContext_t *pCtx, IceControllerRemoteInfo_t *pRemoteInfo )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    MessageQueueResult_t retMessageQueue;
    IceControllerRequestMessage_t requestMessage = {
        .requestType = ICE_CONTROLLER_REQUEST_TYPE_CONNECTIVITY_CHECK,
    };

    if( pRemoteInfo == NULL )
    {
        ret = ICE_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        requestMessage.requestContent.pRemoteInfo = pRemoteInfo;

        retMessageQueue = MessageQueue_Send( &pCtx->requestQueue, &requestMessage, sizeof( IceControllerRequestMessage_t ) );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            ret = ICE_CONTROLLER_RESULT_FAIL_MQ_SEND;
        }
    }

    return ret;
}

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

static IceControllerResult_t handleAddRemoteCandidateRequest( IceControllerContext_t *pCtx, IceControllerRequestMessage_t *pRequestMessage )
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

static IceControllerResult_t handleConnectivityCheckRequest( IceControllerContext_t *pCtx, IceControllerRequestMessage_t *pRequestMessage )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceControllerRemoteInfo_t *pRemoteInfo = pRequestMessage->requestContent.pRemoteInfo;
    IceResult_t iceResult;
    uint32_t i;
    int pairCount;
    /* TODO: not necessary to prepare STUN buffer after fix. */
    char stunBuffer[ 1024 ];
    char transactionIdBuffer[ STUN_HEADER_TRANSACTION_ID_LENGTH ];

    pairCount = Ice_GetValidCandidatePairCount( &pRemoteInfo->iceAgent );
    if( pairCount <= 0 )
    {
        LogError( ( "Fail to query valid candidate pair count, result: %d", iceResult ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_QUERY_CANDIDATE_PAIR_COUNT;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        for( i=0 ; i<pairCount ; i++ )
        {
            iceResult = Ice_CreateRequestForConnectivityCheck( &pRemoteInfo->iceAgent,
                                                               stunBuffer,
                                                               transactionIdBuffer );
            if( iceResult != ICE_RESULT_OK )
            {
                /* Fail to create connectivity check for this round, ignore and continue next round. */
                LogWarn( ( "Fail to create request for connectivity check, result: %d", iceResult ) );
                continue;
            }

            /* TODO: How to get the destination IP address for this round? */
            // ret = IceControllerNet_SendPacket( IceControllerSocketContext_t *pSocketContext, IceIPAddress_t *pDestinationIpAddress, char *pBuffer, size_t length );
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
                    ret = handleAddRemoteCandidateRequest( pCtx, &requestMsg );
                    break;
                case ICE_CONTROLLER_REQUEST_TYPE_CONNECTIVITY_CHECK:
                    ret = handleConnectivityCheckRequest( pCtx, &requestMsg );
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

static IceControllerResult_t parseIceUri( IceControllerIceServer_t *pIceServer, char *pUri, size_t uriLength )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    StringUtilsResult_t retString;
    const char *pCurr, *pTail, *pNext;
    uint32_t port, portStringLength;

    /* Example Ice server URI:
     *  1. turn:35-94-7-249.t-490d1050.kinesisvideo.us-west-2.amazonaws.com:443?transport=udp
     *  2. stun:stun.kinesisvideo.us-west-2.amazonaws.com:443 */
    if( uriLength > ICE_SERVER_TYPE_STUN_LENGTH && strncmp( ICE_SERVER_TYPE_STUN, pUri, ICE_SERVER_TYPE_STUN_LENGTH ) == 0 )
    {
        pIceServer->serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_STUN;
        pTail = pUri + uriLength;
        pCurr = pUri + ICE_SERVER_TYPE_STUN_LENGTH;
    }
    else if( ( uriLength > ICE_SERVER_TYPE_TURNS_LENGTH && strncmp( ICE_SERVER_TYPE_TURNS, pUri, ICE_SERVER_TYPE_TURNS_LENGTH ) == 0 ) )
    {
        pIceServer->serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_TURN;
        pTail = pUri + uriLength;
        pCurr = pUri + ICE_SERVER_TYPE_TURNS_LENGTH;
    }
    else if( uriLength > ICE_SERVER_TYPE_TURN_LENGTH && strncmp( ICE_SERVER_TYPE_TURN, pUri, ICE_SERVER_TYPE_TURN_LENGTH ) == 0 )
    {
        pIceServer->serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_TURN;
        pTail = pUri + uriLength;
        pCurr = pUri + ICE_SERVER_TYPE_TURN_LENGTH;
    }
    else
    {
        /* Invalid server URI, drop it. */
        LogWarn( ( "Unable to parse Ice URI, drop it, URI: %.*s", ( int ) uriLength, pUri ) );
        ret = ICE_CONTROLLER_RESULT_INVALID_ICE_SERVER;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        pNext = memchr( pCurr, ':', pTail - pCurr );
        if( pNext == NULL )
        {
            LogWarn( ( "Unable to find second ':', drop it, URI: %.*s", ( int ) uriLength, pUri ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_ICE_SERVER;
        }
        else
        {
            if( pNext - pCurr >= ICE_CONTROLLER_ICE_SERVER_URL_MAX_LENGTH )
            {
                LogWarn( ( "URL buffer is not enough to store Ice URL, length: %ld", pNext - pCurr ) );
                ret = ICE_CONTROLLER_RESULT_URL_BUFFER_TOO_SMALL;
            }
            else
            {
                memcpy( pIceServer->url, pCurr, pNext - pCurr );
                pIceServer->urlLength = pNext - pCurr;
                /* Note that URL must be NULL terminated for DNS lookup. */
                pIceServer->url[ pIceServer->urlLength ] = '\0';
                pCurr = pNext + 1;
            }
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK && pCurr <= pTail )
    {
        pNext = memchr( pCurr, '?', pTail - pCurr );
        if( pNext == NULL )
        {
            portStringLength = pTail - pCurr;
        }
        else
        {
            portStringLength = pNext - pCurr;
        }

        retString = StringUtils_ConvertStringToUl( pCurr, portStringLength, &port );
        if( retString != STRING_UTILS_RESULT_OK )
        {
            LogWarn( ( "No valid port number, parsed string: %.*s", ( int ) portStringLength, pCurr ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_ICE_SERVER_PORT;
        }
        else
        {
            /* TODO: error handling if it's larger than 16 bit. */
            pIceServer->ipAddress.ipAddress.port = ( uint16_t ) port;
            pCurr += portStringLength;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( pIceServer->serverType == ICE_CONTROLLER_ICE_SERVER_TYPE_TURN && pCurr >= pTail )
        {
            LogWarn( ( "No valid transport string found" ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_ICE_SERVER_PROTOCOL;
        }
        else if( pIceServer->serverType == ICE_CONTROLLER_ICE_SERVER_TYPE_TURN )
        {
            if( strncmp( pCurr, "transport=udp", pTail - pCurr ) == 0 )
            {
                pIceServer->protocol = ICE_SOCKET_PROTOCOL_UDP;
            }
            else if( strncmp( pCurr, "transport=tcp", pTail - pCurr ) == 0 )
            {
                pIceServer->protocol = ICE_SOCKET_PROTOCOL_TCP;
            }
            else
            {
                LogWarn( ( "Unknown transport string found, protocol: %.*s", ( int )( pTail - pCurr ), pCurr ) );
                ret = ICE_CONTROLLER_RESULT_INVALID_ICE_SERVER_PROTOCOL;
            }
        }
        else
        {
            /* Do nothing, coverity happy. */
        }
    }

    /* Use DNS query to get IP address of it. */
    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        ret = IceControllerNet_DnsLookUp( pIceServer->url, &pIceServer->ipAddress.ipAddress );
    }

    return ret;
}

static IceControllerResult_t initializeIceServerList( IceControllerContext_t *pCtx, SignalingControllerContext_t *pSignalingControllerContext )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerIceServerConfig_t *pIceServerConfigs;
    size_t iceServerConfigsCount;
    char *pStunUrlPostfix;
    int written;
    uint32_t i, j;

    signalingControllerReturn = SignalingController_QueryIceServerConfigs( pSignalingControllerContext, &pIceServerConfigs, &iceServerConfigsCount );
    if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
    {
        LogError( ( "Fail to get Ice server configs, result: %d", signalingControllerReturn ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_QUERY_ICE_SERVER_CONFIGS;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        if( strstr( AWS_REGION, "cn-" ) )
        {
            pStunUrlPostfix = AWS_DEFAULT_STUN_SERVER_URL_POSTFIX_CN;
        }
        else
        {
            pStunUrlPostfix = AWS_DEFAULT_STUN_SERVER_URL_POSTFIX;
        }

        /* Get the default STUN server. */
        written = snprintf( pCtx->iceServers[ 0 ].url, ICE_CONTROLLER_ICE_SERVER_URL_MAX_LENGTH, AWS_DEFAULT_STUN_SERVER_URL,
                            AWS_REGION, pStunUrlPostfix );

        if( written < 0 )
        {
            LogError( ( "snprintf fail, errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_SNPRINTF;
        }
        else if( written == ICE_CONTROLLER_ICE_SERVER_URL_MAX_LENGTH )
        {
            LogError( ( "buffer has no space for default STUN server" ) );
            ret = ICE_CONTROLLER_RESULT_STUN_URL_BUFFER_TOO_SMALL;
        }
        else
        {
            /* STUN server is written correctly. Set UDP as protocol since we always use UDP to query server reflexive address. */
            pCtx->iceServers[ 0 ].protocol = ICE_SOCKET_PROTOCOL_UDP;
            pCtx->iceServers[ 0 ].serverType = ICE_CONTROLLER_ICE_SERVER_TYPE_STUN;
            pCtx->iceServers[ 0 ].userNameLength = 0U;
            pCtx->iceServers[ 0 ].passwordLength = 0U;
            pCtx->iceServers[ 0 ].ipAddress.isPointToPoint = 0U;
            pCtx->iceServers[ 0 ].ipAddress.ipAddress.port = 443;
            pCtx->iceServers[ 0 ].url[ written ] = '\0'; /* It must be NULL terminated for DNS query. */
            pCtx->iceServers[ 0 ].urlLength = written;
            pCtx->iceServersCount = 1;

            /* We need to translate DNS into IP address manually because we need IP address as input for socket sendto() function. */
            ret = IceControllerNet_DnsLookUp( pCtx->iceServers[ 0 ].url, &pCtx->iceServers[ 0 ].ipAddress.ipAddress );
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Parse Ice server confgis into IceControllerIceServer_t structure. */
        for( i=0 ; i<iceServerConfigsCount ; i++ )
        {
            /* Drop the URI that is not able to be parsed, but continue parsing. */
            ret = ICE_CONTROLLER_RESULT_OK;

            if( pIceServerConfigs[ i ].userNameLength > ICE_CONTROLLER_ICE_SERVER_USERNAME_MAX_LENGTH )
            {
                LogError( ( "The length of Ice server's username is too long to store, length: %ld", pIceServerConfigs[ i ].userNameLength ) );
                ret = ICE_CONTROLLER_RESULT_USERNAME_BUFFER_TOO_SMALL;
                continue;
            }
            else if( pIceServerConfigs[ i ].passwordLength > ICE_CONTROLLER_ICE_SERVER_PASSWORD_MAX_LENGTH )
            {
                LogError( ( "The length of Ice server's password is too long to store, length: %ld", pIceServerConfigs[ i ].passwordLength ) );
                ret = ICE_CONTROLLER_RESULT_PASSWORD_BUFFER_TOO_SMALL;
                continue;
            }
            else
            {
                /* Do nothing, coverity happy. */
            }

            for( j=0 ; j<pIceServerConfigs[ i ].uriCount ; j++ )
            {
                /* Parse each URI */
                ret = parseIceUri( &pCtx->iceServers[ pCtx->iceServersCount ], pIceServerConfigs[ i ].uris[ j ], pIceServerConfigs[ i ].urisLength[ j ] );
                if( ret != ICE_CONTROLLER_RESULT_OK )
                {
                    continue;
                }

                memcpy( pCtx->iceServers[ pCtx->iceServersCount ].userName, pIceServerConfigs[ i ].userName, pIceServerConfigs[ i ].userNameLength );
                pCtx->iceServers[ pCtx->iceServersCount ].userNameLength = pIceServerConfigs[ i ].userNameLength;
                memcpy( pCtx->iceServers[ pCtx->iceServersCount ].password, pIceServerConfigs[ i ].password, pIceServerConfigs[ i ].passwordLength );
                pCtx->iceServers[ pCtx->iceServersCount ].passwordLength = pIceServerConfigs[ i ].passwordLength;
                pCtx->iceServersCount++;
            }
        }

        /* Ignore latest URI parsing error. */
        ret = ICE_CONTROLLER_RESULT_OK;
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
    TimerControllerResult_t retTimer;

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

    /* Initialize Ice server list. */
    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        ret = initializeIceServerList( pCtx, pSignalingControllerContext );
    }

    /* Initialize request queue for ice controller and attach it into polling fds. */
    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        retMessageQueue = MessageQueue_Create( &pCtx->requestQueue, "/IceController", sizeof( IceControllerRequestMessage_t ), MAX_QUEUE_MSG_NUM );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to open message queue, errno: %s", strerror( errno ) ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_MQ_INIT;
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* We always use index 0 for polling message queue. */
        retMessageQueue = MessageQueue_AttachPoll( &pCtx->requestQueue, &pCtx->fds[ REQUEST_QUEUE_POLL_ID ], POLLIN );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "MessageQueue_AttachPoll return fail, result: %d", retMessageQueue ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_MQ_ATTACH_POLL;
        }
        else
        {
            pCtx->fdsCount = 1;
        }
    }

    /* Initialize timer for connectivity check. */
    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        retTimer = TimerController_Create( &pCtx->connectivityCheckTimer, onConnectivityCheckTimerExpire, pCtx );
        if( retTimer != TIMER_CONTROLLER_RESULT_OK )
        {
            LogError( ( "TimerController_Create return fail, result: %d", retTimer ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_TIMER_INIT;
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
    TimerControllerResult_t retTimer;

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
        /* Initialize Ice controller net. */
        ret = IceControllerNet_InitRemoteInfo( pRemoteInfo );
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        ret = IceControllerNet_AddLocalCandidates( pCtx, pRemoteInfo );
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        retTimer = TimerController_IsTimerSet( &pCtx->connectivityCheckTimer );
        if( retTimer == TIMER_CONTROLLER_RESULT_NOT_SET )
        {
            /* The timer is not set before, start the timer for connectivity check. */
            retTimer = TimerController_SetTimer( &pCtx->connectivityCheckTimer, ICE_CONTROLLER_CONNECTIVITY_TIMER_INTERVAL_MS, ICE_CONTROLLER_CONNECTIVITY_TIMER_INTERVAL_MS );
            if( retTimer != TIMER_CONTROLLER_RESULT_OK )
            {
                LogError( ( "Fail to start connectivity timer, result: %d", retTimer ) );
                ret = ICE_CONTROLLER_RESULT_FAIL_SET_CONNECTIVITY_CHECK_TIMER;
            }
        }
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
    size_t i;
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
            // Poll the message queue descriptor
            pollResult = poll( pCtx->fds, pCtx->fdsCount, POLL_TIMEOUT_MS ); // Wait indefinitely
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

            /* Handle receiving events socket by socket. */
            for( i=REQUEST_QUEUE_POLL_ID + 1 ; i<pCtx->fdsCount ; i++ )
            {
                if( pCtx->fds[ i ].revents & POLLIN )
                {
                    /* Receive socket data, handle it. */
                    ret = IceControllerNet_HandleRxPacket( pCtx->pFdsMapContext[ i ] );
                    if( ret != ICE_CONTROLLER_RESULT_OK )
                    {
                        break;
                    }
                }
            }

            /* Handle message queue. */
            if( pCtx->fds[ REQUEST_QUEUE_POLL_ID ].revents & POLLIN )
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
