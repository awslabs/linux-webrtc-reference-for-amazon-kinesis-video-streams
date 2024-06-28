#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"
#include "ice_api.h"
#include "core_json.h"
#include "string_utils.h"
#include "signaling_controller.h"

#define ICE_CONTROLLER_MESSAGE_QUEUE_NAME "/WebrtcApplicationIceController"

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

static const uint32_t gCrc32Table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

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

static IceResult_t IceController_CalculateRandom( uint8_t * pOutputBuffer,
                                                  size_t outputBufferLength )
{

    size_t i;
    for(i = 0; i < outputBufferLength; i++ )
    {
        pOutputBuffer[i] = ( uint8_t ) ( rand() % 256 );
    }

    return ICE_RESULT_OK;
}

static IceResult_t IceController_CalculateCrc32 ( uint32_t initialResult,
                                                const uint8_t * pBuffer,
                                                size_t bufferLength,
                                                uint32_t * pCalculatedCrc32 )
{
    uint32_t c = initialResult ^ 0xFFFFFFFF, i = 0;

    if( pBuffer == NULL )
    {
        bufferLength = 0;
    }

    for( i = 0; i < bufferLength; ++i )
    {
        c = gCrc32Table[ ( c ^ pBuffer[i] ) & 0xFF ] ^ ( c >> 8 );
    }

    *pCalculatedCrc32 = ( c ^ 0xFFFFFFFF );
    
    return ICE_RESULT_OK;
}

static IceResult_t IceController_OpensslHmac ( const uint8_t * pPassword,
                                                size_t passwordLength,
                                                const uint8_t * pBuffer,
                                                size_t bufferLength,
                                                uint8_t * pOutputBuffer,
                                                uint16_t * pOutputBufferLength )
{
    IceResult_t ret = ICE_RESULT_OK;
    unsigned int outbufLength = ( unsigned int )( *pOutputBufferLength );

    if( HMAC( EVP_sha1(), pPassword, passwordLength, pBuffer, bufferLength, pOutputBuffer, &outbufLength ) == NULL )
    {
        ret = ICE_RESULT_HMAC_ERROR;
        LogError( ( "Something wrong during HMAC" ) );
    }
    else
    {
        *pOutputBufferLength = outbufLength;
    }
    return ret;
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
    IceControllerCandidate_t *pCandidate = (IceControllerCandidate_t *)&pRequestMessage->requestContent.remoteCandidate;
    IceControllerRemoteInfo_t *pRemoteInfo;
    IceRemoteCandidateInfo_t remoteCandidateInfo;

    remoteCandidateInfo.candidateType = pCandidate->candidateType;
    remoteCandidateInfo.pEndpoint = &( pCandidate->iceIpAddress );
    remoteCandidateInfo.priority = pCandidate->priority;
    remoteCandidateInfo.remoteProtocol = pCandidate->protocol;

    printf( "[ handleAddRemoteCandidateRequest ] Remote candidate Address : %x-%x-%x-%x --> Family : %d --> Port : %d\n",
                                                                   remoteCandidateInfo.pEndpoint->transportAddress.address[0],
                                                                   remoteCandidateInfo.pEndpoint->transportAddress.address[1],
                                                                   remoteCandidateInfo.pEndpoint->transportAddress.address[2],
                                                                   remoteCandidateInfo.pEndpoint->transportAddress.address[3],
                                                                   remoteCandidateInfo.pEndpoint->transportAddress.family,
                                                                   remoteCandidateInfo.pEndpoint->transportAddress.port );
    char ipBuffer[ INET_ADDRSTRLEN ];

    /* Find remote info index by mapping remote client ID. */
    ret = findRemoteInfo( pCtx, pCandidate->remoteClientId, pCandidate->remoteClientIdLength, &pRemoteInfo );

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        iceResult = Ice_AddRemoteCandidate( &pRemoteInfo->iceAgent,
                                            &remoteCandidateInfo );
        if( iceResult != ICE_RESULT_OK )
        {
            LogError( ( "Fail to add remote candidate, result: %d", iceResult ) );
            ret = ICE_CONTROLLER_RESULT_FAIL_ADD_REMOTE_CANDIDATE;
        }
        else
        {
            LogDebug( ( "Received remote candidate with IP/port: %s/%d",
                        IceControllerNet_LogIpAddressInfo( remoteCandidateInfo.pEndpoint, ipBuffer, sizeof( ipBuffer ) ),
                        remoteCandidateInfo.pEndpoint->transportAddress.port ) );
        }
    }

    return ret;
}

static IceControllerSocketContext_t *findSocketContextByLocalCandidate( IceControllerRemoteInfo_t *pRemoteInfo, IceCandidate_t *pLocalCandidate )
{
    IceControllerSocketContext_t *pReturnContext = NULL;
    uint32_t i;

    if( pLocalCandidate != NULL )
    {
        for( i=0 ; i<pRemoteInfo->socketsContextsCount ; i++ )
        {
            if( pRemoteInfo->socketsContexts[i].pLocalCandidate == pLocalCandidate )
            {
                pReturnContext = &pRemoteInfo->socketsContexts[i];
            }
        }
    }

    return pReturnContext;
}

static IceControllerResult_t handleConnectivityCheckRequest( IceControllerContext_t *pCtx, IceControllerRequestMessage_t *pRequestMessage )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    IceControllerRemoteInfo_t *pRemoteInfo = pRequestMessage->requestContent.pRemoteInfo;
    IceResult_t iceResult;
    uint32_t i;
    size_t pairCount;
    uint8_t stunBuffer[ 1024 ];
    size_t stunBufferLength = 1024;
    char transactionIdBuffer[ STUN_HEADER_TRANSACTION_ID_LENGTH ];
    IceControllerSocketContext_t *pSocketContext;
    char ipFromBuffer[ INET_ADDRSTRLEN ];
    char ipToBuffer[ INET_ADDRSTRLEN ];

    if( pCtx->metrics.isFirstConnectivityRequest == 1 )
    {
        pCtx->metrics.isFirstConnectivityRequest = 0;
        gettimeofday( &pCtx->metrics.firstConnectivityRequestTime, NULL );
    }

    iceResult = Ice_GetCandidatePairCount( &pRemoteInfo->iceAgent, &pairCount );
    if( iceResult != ICE_RESULT_OK )
    {
        LogError( ( "Fail to query valid candidate pair count, result: %d", iceResult ) );
        ret = ICE_CONTROLLER_RESULT_FAIL_QUERY_CANDIDATE_PAIR_COUNT;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        for( i=0 ; i<pairCount ; i++ )
        {
            ret = ICE_CONTROLLER_RESULT_OK;

            iceResult = Ice_CreateRequestForConnectivityCheck( &pRemoteInfo->iceAgent,
                                                               &pRemoteInfo->iceAgent.pCandidatePairs[i],
                                                               &stunBuffer[ 0 ],
                                                               &stunBufferLength);

            if( iceResult != ICE_RESULT_OK )
            {
                /* Fail to create connectivity check for this round, ignore and continue next round. */
                LogWarn( ( "Fail to create request for connectivity check, result: %d", iceResult ) );
                exit(0);
                continue;
            }
            else if( pRemoteInfo->iceAgent.pCandidatePairs[i].pRemoteCandidate == NULL )
            {
                /* No remote candidate mapped to this pair, ignore and continue next round. */
                LogWarn( ( "No remote candidate available for this pair, skip this pair" ) );
                continue;
            }
            else
            {
                /* Do nothing, coverity happy. */
            }

            pSocketContext = findSocketContextByLocalCandidate( pRemoteInfo, pRemoteInfo->iceAgent.pCandidatePairs[i].pLocalCandidate );
            if( pSocketContext == NULL )
            {
                LogWarn( ( "Not able to find socket context mapping, mapping local candidate: %p", pRemoteInfo->iceAgent.pCandidatePairs[i].pLocalCandidate ) );
                continue;
            }

            LogDebug( ( "Sending connecitivity check from IP/port: %s/%d to %s/%d",
                        IceControllerNet_LogIpAddressInfo( &pRemoteInfo->iceAgent.pCandidatePairs[i].pLocalCandidate->endpoint, ipFromBuffer, sizeof( ipFromBuffer ) ),
                        pRemoteInfo->iceAgent.pCandidatePairs[i].pLocalCandidate->endpoint.transportAddress.port,
                        IceControllerNet_LogIpAddressInfo( &pRemoteInfo->iceAgent.pCandidatePairs[i].pRemoteCandidate->endpoint, ipToBuffer, sizeof( ipToBuffer ) ),
                        pRemoteInfo->iceAgent.pCandidatePairs[i].pRemoteCandidate->endpoint.transportAddress.port ) );
            IceControllerNet_LogStunPacket( &stunBuffer[ 0 ], stunBufferLength );

            ret = IceControllerNet_SendPacket( pSocketContext, &pRemoteInfo->iceAgent.pCandidatePairs[i].pRemoteCandidate->endpoint, &stunBuffer[ 0 ], stunBufferLength );
            if( ret != ICE_CONTROLLER_RESULT_OK )
            {
                LogWarn( ( "Unable to send packet to remote address, result: %d", ret ) );
                continue;
            }
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
        if( retString != STRING_UTILS_RESULT_OK || port > UINT16_MAX )
        {
            LogWarn( ( "No valid port number, parsed string: %.*s", ( int ) portStringLength, pCurr ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_ICE_SERVER_PORT;
        }
        else
        {
            pIceServer->ipAddress.transportAddress.port = ( uint16_t ) port;
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
        ret = IceControllerNet_DnsLookUp( pIceServer->url, &pIceServer->ipAddress.transportAddress );
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
            pCtx->iceServers[ 0 ].ipAddress.transportAddress.port = 443;
            pCtx->iceServers[ 0 ].url[ written ] = '\0'; /* It must be NULL terminated for DNS query. */
            pCtx->iceServers[ 0 ].urlLength = written;
            pCtx->iceServersCount = 1;

            /* We need to translate DNS into IP address manually because we need IP address as input for socket sendto() function. */
            ret = IceControllerNet_DnsLookUp( pCtx->iceServers[ 0 ].url, &pCtx->iceServers[ 0 ].ipAddress.transportAddress );
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

void IceController_PrintMetrics( IceControllerContext_t * pCtx )
{
    uint8_t i, j;
    long long duration_ms;

    /* Print each step duration */
    LogDebug( ( "======================================== Ice Duration ========================================" ) );
    duration_ms = (pCtx->metrics.gatheringCandidateEndTime.tv_sec - pCtx->metrics.gatheringCandidateStartTime.tv_sec) * 1000LL +
                  (pCtx->metrics.gatheringCandidateEndTime.tv_usec - pCtx->metrics.gatheringCandidateStartTime.tv_usec) / 1000LL;
    LogDebug( ( "Duration from Starting Gathering Candidates to All Host Candidates Ready: %lld ms", duration_ms ) );
    duration_ms = (pCtx->metrics.allSrflxCandidateReadyTime.tv_sec - pCtx->metrics.gatheringCandidateStartTime.tv_sec) * 1000LL +
                  (pCtx->metrics.allSrflxCandidateReadyTime.tv_usec - pCtx->metrics.gatheringCandidateStartTime.tv_usec) / 1000LL;
    LogDebug( ( "Duration from Starting Gathering Candidates to All Server Candidates Ready: %lld ms", duration_ms ) );
    duration_ms = (pCtx->metrics.sentNominationResponseTime.tv_sec - pCtx->metrics.firstConnectivityRequestTime.tv_sec) * 1000LL +
                  (pCtx->metrics.sentNominationResponseTime.tv_usec - pCtx->metrics.firstConnectivityRequestTime.tv_usec) / 1000LL;
    LogDebug( ( "Duration from Starting Connectivity Check to Sent Nomination Response: %lld ms", duration_ms ) );
    duration_ms = (pCtx->metrics.sentNominationResponseTime.tv_sec - pCtx->metrics.gatheringCandidateStartTime.tv_sec) * 1000LL +
                  (pCtx->metrics.sentNominationResponseTime.tv_usec - pCtx->metrics.gatheringCandidateStartTime.tv_usec) / 1000LL;
    LogDebug( ( "Duration of entire Ice flow: %lld ms", duration_ms ) );
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
        MessageQueue_Destroy( &pCtx->requestQueue, ICE_CONTROLLER_MESSAGE_QUEUE_NAME );
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
        // generateJSONValidString( pCtx->localUserName, ICE_CONTROLLER_USER_NAME_LENGTH );
        // pCtx->localUserName[ ICE_CONTROLLER_USER_NAME_LENGTH ] = '\0';
        // generateJSONValidString( pCtx->localPassword, ICE_CONTROLLER_PASSWORD_LENGTH );
        // pCtx->localPassword[ ICE_CONTROLLER_PASSWORD_LENGTH ] = '\0';
        // TODO
        memcpy( pCtx->localUserName, "GnjB", ICE_CONTROLLER_USER_NAME_LENGTH );
        pCtx->localUserName[ ICE_CONTROLLER_USER_NAME_LENGTH ] = '\0';
        memcpy( pCtx->localPassword, "eu8hMmfpUkEU3t1DfJb+/J3e", ICE_CONTROLLER_PASSWORD_LENGTH );
        pCtx->localPassword[ ICE_CONTROLLER_PASSWORD_LENGTH ] = '\0';

        pCtx->pSignalingControllerContext = pSignalingControllerContext;

        /* Initialize metrics. */
        pCtx->metrics.isFirstConnectivityRequest = 1;
    }

    /* Initialize Ice server list. */
    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        ret = initializeIceServerList( pCtx, pSignalingControllerContext );
    }

    /* Initialize request queue for ice controller and attach it into polling fds. */
    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Delete message queue from previous round. */
        MessageQueue_Destroy( NULL, ICE_CONTROLLER_MESSAGE_QUEUE_NAME );

        retMessageQueue = MessageQueue_Create( &pCtx->requestQueue, ICE_CONTROLLER_MESSAGE_QUEUE_NAME, sizeof( IceControllerRequestMessage_t ), MAX_QUEUE_MSG_NUM );
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
                uint32_t localPort = 0;
                stringResult = StringUtils_ConvertStringToUl( pCurr, tokenLength, (uint32_t *) &localPort );
                pCandidate->port = ( uint16_t ) localPort;
                if( stringResult != STRING_UTILS_RESULT_OK )
                {
                    LogWarn( ( "Invalid port %.*s",
                                ( int ) tokenLength, pCurr ) );
                    ret = ICE_CONTROLLER_RESULT_JSON_CANDIDATE_INVALID_PORT;
                }
                else
                {
                    pCandidate->iceIpAddress.transportAddress.port = pCandidate->port;
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
    IceControllerRemoteInfo_t *pRemoteInfo;
    IceInitInfo_t *pIceInitInfo;
    TimerControllerResult_t retTimer;
    IceCandidate_t *pLocalCandidates;
    IceCandidate_t *pRemoteCandidates;
    IceCandidatePair_t *pCandidatePairs;

    if( pCtx == NULL || pRemoteClientId == NULL ||
        pRemoteUserName == NULL || pRemotePassword == NULL ||
        remoteUserNameLength > ICE_MAX_CONFIG_USER_NAME_LEN ||
        remotePasswordLength > ICE_MAX_CONFIG_CREDENTIAL_LEN )
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
        if( remoteUserNameLength + ICE_CONTROLLER_USER_NAME_LENGTH > ( ICE_MAX_CONFIG_USER_NAME_LEN << 1 ) )
        {
            LogWarn( ( "Remote user name is too long to store, length: %ld", remoteUserNameLength ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_REMOTE_USERNAME;
        }
        else
        {
            memcpy( pCtx->remoteUserName, pRemoteUserName, remoteUserNameLength );
            pCtx->remoteUserName[ remoteUserNameLength ] = '\0';
            memcpy( pCtx->remotePassword, pRemotePassword, remotePasswordLength );
            pCtx->remotePassword[ remotePasswordLength ] = '\0';
            snprintf( pCtx->combinedName, sizeof( pCtx->combinedName ), "%s:%s",
                      pCtx->remoteUserName, pCtx->localUserName );

            TransactionIdStore_Init( &pRemoteInfo->transactionIdStore, &pRemoteInfo->transactionIds[0], ICE_MAX_CANDIDATE_PAIR_COUNT );

            /* Creating the Ice Initialization Info. */

            pIceInitInfo = malloc( sizeof( IceInitInfo_t ) );
            memset( pIceInitInfo, 0, sizeof( IceInitInfo_t ) );
            pLocalCandidates = malloc( ICE_MAX_LOCAL_CANDIDATE_COUNT * sizeof( IceCandidate_t ) );
            pRemoteCandidates = malloc( ICE_MAX_REMOTE_CANDIDATE_COUNT * sizeof( IceCandidate_t ) );
            pCandidatePairs = malloc( ICE_MAX_CANDIDATE_PAIR_COUNT * sizeof( IceCandidatePair_t ) );

            pIceInitInfo->creds.pLocalUsername = pCtx->localUserName;
            pIceInitInfo->creds.localUsernameLength = strlen( pCtx->localUserName );
            pIceInitInfo->creds.pLocalPassword = pCtx->localPassword;
            pIceInitInfo->creds.localPasswordLength = strlen( pCtx->localPassword );
            pIceInitInfo->creds.pRemoteUsername = pCtx->remoteUserName;
            pIceInitInfo->creds.remoteUsernameLength = strlen( pCtx->remoteUserName );
            pIceInitInfo->creds.pRemotePassword = pCtx->remotePassword;
            pIceInitInfo->creds.remotePasswordLength = strlen( pCtx->remotePassword );
            pIceInitInfo->creds.pCombinedUsername = pCtx->combinedName;
            pIceInitInfo->creds.combinedUsernameLength = strlen( pCtx->combinedName );
            pIceInitInfo->pLocalCandidatesArray = pLocalCandidates;
            pIceInitInfo->pRemoteCandidatesArray = pRemoteCandidates;
            pIceInitInfo->pCandidatePairsArray = pCandidatePairs;
            pIceInitInfo->candidatePairsArrayLength = ICE_MAX_CANDIDATE_PAIR_COUNT;
            pIceInitInfo->localCandidatesArrayLength = ICE_MAX_LOCAL_CANDIDATE_COUNT;
            pIceInitInfo->remoteCandidatesArrayLength = ICE_MAX_REMOTE_CANDIDATE_COUNT;
            pIceInitInfo->cryptoFunctions.randomFxn = IceController_CalculateRandom;
            pIceInitInfo->cryptoFunctions.crc32Fxn = IceController_CalculateCrc32;
            pIceInitInfo->cryptoFunctions.hmacFxn = IceController_OpensslHmac;
            pIceInitInfo->isControlling = 0;
            pIceInitInfo->pStunBindingRequestTransactionIdStore = &pRemoteInfo->transactionIdStore;

            iceResult = Ice_Init( &pRemoteInfo->iceAgent,
                                  pIceInitInfo );

            if( iceResult != ICE_RESULT_OK )
            {
                LogError( ( "Fail to create ICE agent, result: %d", iceResult ) );
                ret = ICE_CONTROLLER_RESULT_FAIL_CREATE_ICE_AGENT;
            }
        }
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        /* Initialize Ice controller net. */
        ret = IceControllerNet_InitRemoteInfo( pRemoteInfo );
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        gettimeofday( &pCtx->metrics.gatheringCandidateStartTime, NULL );
        ret = IceControllerNet_AddLocalCandidates( pCtx, pRemoteInfo );
        gettimeofday( &pCtx->metrics.gatheringCandidateEndTime, NULL );
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        retTimer = TimerController_IsTimerSet( &pCtx->connectivityCheckTimer );
        if( retTimer == TIMER_CONTROLLER_RESULT_NOT_SET )
        {
            /* The timer is not set before, send the request immendiately and start connectivity check timer. */
            onConnectivityCheckTimerExpire( pCtx );
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
                    ret = IceControllerNet_HandleRxPacket( pCtx, pCtx->pFdsMapContext[ i ] );
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
