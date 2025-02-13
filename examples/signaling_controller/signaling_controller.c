#include <string.h>
#include "logging.h"
#include "signaling_controller.h"
#include "signaling_api.h"
#include "base64.h"
#include "metric.h"
#include "core_json.h"
#include "networking_utils.h"

#ifndef MIN
#define MIN( a,b ) ( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#endif

#define SIGNALING_CONTROLLER_MESSAGE_QUEUE_NAME "/WebrtcApplicationSignalingController"

#define MAX_QUEUE_MSG_NUM ( 10 )
#define WEBSOCKET_ENDPOINT_PORT ( 443U )
#define HTTPS_PERFORM_RETRY_TIMES ( 5U )

#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_KEY "type"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_OFFER "offer"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_ANSWER "answer"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_CONTENT_KEY "sdp"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_NEWLINE_ENDING "\\n"

static SignalingControllerResult_t UpdateIceServerConfigs( SignalingControllerContext_t * pCtx,
                                                           SignalingIceServer_t * pIceServerList,
                                                           size_t iceServerListNum );

static int HandleWssMessage( char * pMessage,
                             size_t messageLength,
                             void * pUserData )
{
    int ret = 0;
    SignalingResult_t retSignal;
    WssRecvMessage_t wssRecvMessage;
    SignalingControllerContext_t * pCtx = ( SignalingControllerContext_t * ) pUserData;
    SignalingControllerReceiveEvent_t receiveEvent;
    Base64Result_t retBase64;
    bool needCallback = true;

    if( ( pMessage == NULL ) || ( messageLength == 0 ) )
    {
        LogWarn( ( "Received empty signaling message" ) );
        ret = 1;
    }

    if( ret == 0 )
    {
        // Parse the response
        retSignal = Signaling_ParseWssRecvMessage( pMessage, ( size_t ) messageLength, &wssRecvMessage );
        if( retSignal != SIGNALING_RESULT_OK )
        {
            LogError( ( "Fail to parse the WSS message, result: %d", retSignal ) );
            ret = 1;
        }
    }

    /* Decode base64 payload. */
    if( ret == 0 )
    {
        pCtx->base64BufferLength = SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH;
        retBase64 = Base64_Decode( wssRecvMessage.pBase64EncodedPayload, wssRecvMessage.base64EncodedPayloadLength, pCtx->base64Buffer, &pCtx->base64BufferLength );

        if( retBase64 != BASE64_RESULT_OK )
        {
            LogError( ( "Fail to decode base64, result: %d", retBase64 ) );
            ret = 1;
        }
    }

    if( ret == 0 )
    {
        switch( wssRecvMessage.messageType )
        {
            case SIGNALING_TYPE_MESSAGE_GO_AWAY:
                LogInfo( ( "Received GOAWAY frame from server. Closing connection." ) );
                ret = 1;
                break;

            case SIGNALING_TYPE_MESSAGE_STATUS_RESPONSE:

                if( strcmp( wssRecvMessage.statusResponse.pStatusCode,"200" ) != 0 )
                {
                    LogWarn( ( "Failed to deliver message. Correlation ID: %s, Error Type: %s, Error Code: %s, Description: %s",
                               wssRecvMessage.statusResponse.pCorrelationId,
                               wssRecvMessage.statusResponse.pErrorType,
                               wssRecvMessage.statusResponse.pStatusCode,
                               wssRecvMessage.statusResponse.pDescription ) );
                }
                else
                {

                }
                break;
            default:
                break;
        }
    }

    if( ret == 0 )
    {
        memset( &receiveEvent, 0, sizeof( SignalingControllerReceiveEvent_t ) );
        receiveEvent.pRemoteClientId = wssRecvMessage.pSenderClientId;
        receiveEvent.remoteClientIdLength = wssRecvMessage.senderClientIdLength;
        receiveEvent.pCorrelationId = wssRecvMessage.statusResponse.pCorrelationId;
        receiveEvent.correlationIdLength = wssRecvMessage.statusResponse.correlationIdLength;
        receiveEvent.messageType = wssRecvMessage.messageType;
        receiveEvent.pDecodeMessage = pCtx->base64Buffer;
        receiveEvent.decodeMessageLength = pCtx->base64BufferLength;

        if( ( needCallback == true ) && ( pCtx->receiveMessageCallback != NULL ) )
        {
            pCtx->receiveMessageCallback( &receiveEvent, pCtx->pReceiveMessageCallbackContext );
        }
    }

    return ret;
}

static SignalingControllerResult_t SignalingController_HttpPerform( SignalingControllerContext_t * pCtx,
                                                                    HttpRequest_t * pRequest,
                                                                    size_t timeoutMs,
                                                                    HttpResponse_t * pResponse )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    NetworkingResult_t networkingResult;
    AwsCredentials_t awsCreds;
    int i;

    pRequest->pUserAgent = pCtx->credential.userAgentName;
    pRequest->userAgentLength = pCtx->credential.userAgentNameLength;

    awsCreds.pAccessKeyId = pCtx->credential.accessKeyId;
    awsCreds.accessKeyIdLen = pCtx->credential.accessKeyIdLength;

    awsCreds.pSecretAccessKey = pCtx->credential.secretAccessKey;
    awsCreds.secretAccessKeyLen = pCtx->credential.secretAccessKeyLength;

    awsCreds.pRegion = pCtx->credential.region;
    awsCreds.regionLen = pCtx->credential.regionLength;

    awsCreds.pService = "kinesisvideo";
    awsCreds.serviceLen = strlen( awsCreds.pService );

    awsCreds.pSessionToken = pCtx->credential.sessionToken;
    awsCreds.sessionTokenLength = pCtx->credential.sessionTokenLength;
    awsCreds.expirationSeconds = pCtx->credential.expirationSeconds;

    pRequest->verb = HTTP_POST;

    for( i = 0; i < HTTPS_PERFORM_RETRY_TIMES; i++ )
    {
       networkingResult = Networking_HttpSend( &( pCtx->networkingContext ),
                                               pRequest,
                                               &( awsCreds ),
                                               pResponse );

        if( networkingResult == NETWORKING_RESULT_OK )
        {
            break;
        }
    }

    if( networkingResult != NETWORKING_RESULT_OK )
    {
        LogError( ( "Http_Send fails with return 0x%x", networkingResult ) );
        ret = SIGNALING_CONTROLLER_RESULT_HTTP_PERFORM_REQUEST_FAIL;
    }

    return ret;
}

static void printMetrics( SignalingControllerContext_t * pCtx )
{
    uint8_t i, j;

    /* channel info */
    LogInfo( ( "======================================== Channel Info ========================================" ) );
    LogInfo( ( "Signaling Channel Name: %s", pCtx->channelInfo.signalingChannelName ) );
    LogInfo( ( "Signaling Channel ARN: %s", pCtx->channelInfo.signalingChannelARN ) );
    LogInfo( ( "Signaling Channel TTL (seconds): %u", pCtx->channelInfo.signalingChannelTtlSeconds ) );
    LogInfo( ( "======================================== Endpoints Info ========================================" ) );
    LogInfo( ( "HTTPS Endpoint: %s", pCtx->channelInfo.endpointHttps ) );
    LogInfo( ( "WSS Endpoint: %s", pCtx->channelInfo.endpointWebsocketSecure ) );
    LogInfo( ( "WebRTC Endpoint: %s", pCtx->channelInfo.endpointWebrtc[0] == '\0' ? "N/A" : pCtx->channelInfo.endpointWebrtc ) );

    /* Ice server list */
    LogInfo( ( "======================================== Ice Server List ========================================" ) );
    LogInfo( ( "Ice Server Count: %u", pCtx->iceServerConfigsCount ) );
    for( i = 0; i < pCtx->iceServerConfigsCount; i++ )
    {
        LogInfo( ( "======================================== Ice Server[%u] ========================================", i ) );
        LogInfo( ( "    TTL (seconds): %u", pCtx->iceServerConfigs[i].ttlSeconds ) );
        LogInfo( ( "    User Name: %s", pCtx->iceServerConfigs[i].userName ) );
        LogInfo( ( "    Password: %s", pCtx->iceServerConfigs[i].password ) );
        LogInfo( ( "    URI Count: %u", pCtx->iceServerConfigs[i].uriCount ) );
        for( j = 0; j < pCtx->iceServerConfigs[i].uriCount; j++ )
        {
            LogInfo( ( "        URI: %s", pCtx->iceServerConfigs[i].uris[j] ) );
        }
    }
}

static SignalingControllerResult_t UpdateIceServerConfigs( SignalingControllerContext_t * pCtx,
                                                           SignalingIceServer_t * pIceServerList,
                                                           size_t iceServerListNum )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    uint64_t iceServerConfigTimeSec;
    uint64_t minTTL = UINT64_MAX;
    uint8_t i;
    uint8_t j;

    if( ( pCtx == NULL ) || ( pIceServerList == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        for( i = 0; i < iceServerListNum; i++ )
        {
            if( i >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT )
            {
                break;
            }
            else if( pIceServerList[i].userNameLength >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_USER_NAME_LENGTH )
            {
                LogError( ( "The length of user name of ice server is too long to store, length=%lu", pIceServerList[i].userNameLength ) );
                ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_USERNAME;
                break;
            }
            else if( pIceServerList[i].passwordLength >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_PASSWORD_LENGTH )
            {
                LogError( ( "The length of password of ice server is too long to store, length=%lu", pIceServerList[i].passwordLength ) );
                ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_PASSWORD;
                break;
            }
            else
            {
                /* Do nothing, coverity happy. */
            }

            memcpy( pCtx->iceServerConfigs[i].userName, pIceServerList[i].pUserName, pIceServerList[i].userNameLength );
            pCtx->iceServerConfigs[i].userNameLength = pIceServerList[i].userNameLength;
            memcpy( pCtx->iceServerConfigs[i].password, pIceServerList[i].pPassword, pIceServerList[i].passwordLength );
            pCtx->iceServerConfigs[i].passwordLength = pIceServerList[i].passwordLength;
            pCtx->iceServerConfigs[i].ttlSeconds = pIceServerList[i].messageTtlSeconds;

            minTTL = MIN( minTTL, pCtx->iceServerConfigs[i].ttlSeconds );

            for( j = 0; j < pIceServerList[i].urisNum; j++ )
            {
                if( j >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT )
                {
                    break;
                }
                else if( pIceServerList[i].urisLength[j] >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_URI_LENGTH )
                {
                    LogError( ( "The length of URI of ice server is too long to store, length=%lu", pIceServerList[i].urisLength[j] ) );
                    ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_URI;
                    break;
                }
                else
                {
                    /* Do nothing, coverity happy. */
                }

                memcpy( &pCtx->iceServerConfigs[i].uris[j], pIceServerList[i].pUris[j], pIceServerList[i].urisLength[j] );
                pCtx->iceServerConfigs[i].urisLength[j] = pIceServerList[i].urisLength[j];
            }

            if( ret == SIGNALING_CONTROLLER_RESULT_OK )
            {
                pCtx->iceServerConfigs[i].uriCount = j;
            }
            else
            {
                break;
            }
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Update context with latest ICE server configuration, including server count and expiration. */
        pCtx->iceServerConfigsCount = i;

        iceServerConfigTimeSec = NetworkingUtils_GetCurrentTimeSec( NULL );

        if( minTTL < ICE_CONFIGURATION_REFRESH_GRACE_PERIOD_SEC )
        {
            LogWarn( ( "Minimum TTL is less than Refresh Grace Period." ) );
        }

        pCtx->iceServerConfigExpirationSec = iceServerConfigTimeSec + ( minTTL - ICE_CONFIGURATION_REFRESH_GRACE_PERIOD_SEC );
    }

    return ret;
}

static SignalingControllerResult_t FetchTemporaryCredentials( SignalingControllerContext_t * pCtx,
                                                              SignalingControllerCredential_t * pCredential )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingRequest_t signalRequest;
    HttpRequest_t request;
    HttpResponse_t response;
    SignalingCredential_t retCredentials;
    HttpRequestHeader_t header;

    // Prepare URL buffer
    signalRequest.pUrl = pCtx->httpUrlBuffer;
    signalRequest.urlLength = SIGNALING_CONTROLLER_MAX_HTTP_URI_LENGTH;

    if( ( pCtx == NULL ) || ( pCredential == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: 0x%p, pCredential: 0x%p", pCtx, pCredential ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    retSignal = Signaling_ConstructFetchTempCredsRequestForAwsIot( pCredential->credEndpoint,
                                                                    pCredential->credEndpointLength,
                                                                    pCredential->iotThingRoleAlias,
                                                                    pCredential->iotThingRoleAliasLength,
                                                                    &signalRequest );

    if( retSignal != SIGNALING_RESULT_OK )
    {
        LogError( ( "Fail to construct Fetch Temporary Credential request, return=0x%x", retSignal ) );
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_DESCRIBE_SIGNALING_CHANNEL_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        header.pName = "x-amzn-iot-thingname";
        header.pValue = pCredential->iotThingName;
        header.valueLength = pCredential->iotThingNameLength;

        memset( &request, 0, sizeof( HttpRequest_t ) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pHeaders = &( header );
        request.numHeaders = 1;
        request.pUserAgent = pCtx->credential.userAgentName;
        request.userAgentLength = pCtx->credential.userAgentNameLength;
        request.verb = HTTP_GET;
        //request.isFetchingCredential = 1U;

        memset( &response, 0, sizeof( HttpResponse_t ) );
        response.pBuffer = pCtx->httpResponserBuffer;
        response.bufferLength = SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH;

        if( Networking_HttpSend( &( pCtx->networkingContext ),
                                 &( request ),
                                 NULL,
                                 &( response ) ) != NETWORKING_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_HTTP_PERFORM_REQUEST_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retSignal = Signaling_ParseFetchTempCredsResponseFromAwsIot( response.pBuffer,
                                                                     response.bufferLength,
                                                                     &retCredentials );

        if( retSignal != SIGNALING_RESULT_OK )
        {
            LogError( ( "Fail to parse fetching credentials response, return=0x%x, response(%lu): %.*s", retSignal, response.bufferLength,
                        ( int ) response.bufferLength, response.pBuffer ) );
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_TEMPORARY_CREDENTIALS_FAIL;
        }
        else
        {
            LogDebug( ( "Access Key ID : %.*s \n \n Secret Access Key ID : %.*s \n \n Session Token : %.*s \n \n Expiration : %.*s",
                        ( int ) retCredentials.accessKeyIdLength, retCredentials.pAccessKeyId,
                        ( int ) retCredentials.secretAccessKeyLength, retCredentials.pSecretAccessKey,
                        ( int ) retCredentials.sessionTokenLength, retCredentials.pSessionToken,
                        ( int ) retCredentials.expirationLength, ( char * ) retCredentials.pExpiration ) );
        }
    }

    // Parse the response
    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( retCredentials.pAccessKeyId != NULL ) )
    {
        if( retCredentials.accessKeyIdLength > SIGNALING_CONTROLLER_ACCESS_KEY_ID_MAX_LENGTH )
        {
            /* Return Access Key is longer than expectation. Drop it. */
            LogError( ( "Length of Access Key ID(%lu) is out of maximum value.",
                        retCredentials.accessKeyIdLength ) );
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_TEMPORARY_CREDENTIALS_FAIL;
        }
        else
        {
            memcpy( pCredential->accessKeyId, retCredentials.pAccessKeyId, retCredentials.accessKeyIdLength );
            pCredential->accessKeyIdLength = retCredentials.accessKeyIdLength;
            pCredential->accessKeyId[ pCredential->accessKeyIdLength ] = '\0';
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( retCredentials.pSecretAccessKey != NULL ) )
    {
        if( retCredentials.secretAccessKeyLength > SIGNALING_CONTROLLER_SECRET_ACCESS_KEY_MAX_LENGTH )
        {
            /* Return Secret Access Key is longer than expectation. Drop it. */
            LogError( ( "Secret Access Key Greater than MAX Length. " ) );
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_TEMPORARY_CREDENTIALS_FAIL;
        }
        else
        {
            memcpy( pCredential->secretAccessKey, retCredentials.pSecretAccessKey, retCredentials.secretAccessKeyLength );
            pCredential->secretAccessKeyLength = retCredentials.secretAccessKeyLength;
            pCredential->secretAccessKey[ pCredential->secretAccessKeyLength ] = '\0';
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( retCredentials.pSessionToken != NULL ) )
    {
        if( retCredentials.sessionTokenLength > SIGNALING_CONTROLLER_AWS_MAX_SESSION_TOKEN_LENGTH )
        {
            /* Return Session Token is longer than expectation. Drop it. */
            LogError( ( "Session Token Greater than MAX Length. " ) );
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_TEMPORARY_CREDENTIALS_FAIL;
        }
        else
        {
            memcpy( pCredential->sessionToken, retCredentials.pSessionToken, retCredentials.sessionTokenLength );
            pCredential->sessionTokenLength = retCredentials.sessionTokenLength;
            pCredential->sessionToken[ pCredential->sessionTokenLength ] = '\0';
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( retCredentials.pExpiration != NULL ) )
    {
        if( retCredentials.expirationLength > EXPIRATION_MAX_LEN )
        {
            /* Return Expiration for Access Key's is longer than expectation. Drop it. */
            LogError( ( "Expiration for Access Key's Greater than MAX Length. " ) );
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_TEMPORARY_CREDENTIALS_FAIL;
        }
        else
        {
            pCredential->expirationSeconds = NetworkingUtils_GetTimeFromIso8601( retCredentials.pExpiration, retCredentials.expirationLength );
        }
    }

    return ret;
}
static SignalingControllerResult_t describeSignalingChannel( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingAwsRegion_t awsRegion;
    SignalingChannelName_t channelName;
    SignalingRequest_t signalRequest;
    HttpRequest_t request;
    HttpResponse_t response;
    SignalingChannelInfo_t channelInfo;

    // Prepare AWS region
    awsRegion.pAwsRegion = pCtx->credential.region;
    awsRegion.awsRegionLength = pCtx->credential.regionLength;
    // Prepare channel name
    channelName.pChannelName = pCtx->credential.channelName;
    channelName.channelNameLength = pCtx->credential.channelNameLength;
    // Prepare URL buffer
    signalRequest.pUrl = pCtx->httpUrlBuffer;
    signalRequest.urlLength = SIGNALING_CONTROLLER_MAX_HTTP_URI_LENGTH;
    // Prepare body buffer
    signalRequest.pBody = pCtx->httpBodyBuffer;
    signalRequest.bodyLength = SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH;

    retSignal = Signaling_ConstructDescribeSignalingChannelRequest( &awsRegion,
                                                                    &channelName,
                                                                    &signalRequest );

    if( retSignal != SIGNALING_RESULT_OK )
    {
        LogError( ( "Fail to construct describe signaling channel request, return=0x%x", retSignal ) );
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_DESCRIBE_SIGNALING_CHANNEL_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &request, 0, sizeof( HttpRequest_t ) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;
        //request.isFetchingCredential = 0U;

        memset( &response, 0, sizeof( HttpResponse_t ) );
        response.pBuffer = pCtx->httpResponserBuffer;
        response.bufferLength = SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH;

        ret = SignalingController_HttpPerform( pCtx, &request, 0, &response );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retSignal = Signaling_ParseDescribeSignalingChannelResponse( response.pBuffer, response.bufferLength, &channelInfo );

        if( retSignal != SIGNALING_RESULT_OK )
        {
            LogError( ( "Fail to parse describe signaling channel response, return=0x%x, response(%lu): %.*s", retSignal, response.bufferLength,
                        ( int ) response.bufferLength, response.pBuffer ) );
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_DESCRIBE_SIGNALING_CHANNEL_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( ( channelInfo.pChannelStatus == NULL ) || ( strncmp( channelInfo.pChannelStatus, "ACTIVE", channelInfo.channelStatusLength ) != 0 ) )
        {
            LogError( ( "No active channel status found." ) );
            ret = SIGNALING_CONTROLLER_RESULT_INACTIVE_SIGNALING_CHANNEL;
        }
    }

    // Parse the response
    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( channelInfo.channelArn.pChannelArn != NULL ) )
    {
        if( channelInfo.channelArn.channelArnLength > SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH )
        {
            /* Return ARN is longer than expectation. Drop it. */
            LogError( ( "No active channel status found." ) );
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_ARN;
        }
        else
        {
            strncpy( pCtx->channelInfo.signalingChannelARN, channelInfo.channelArn.pChannelArn, channelInfo.channelArn.channelArnLength );
            pCtx->channelInfo.signalingChannelARN[ channelInfo.channelArn.channelArnLength ] = '\0';
            pCtx->channelInfo.signalingChannelARNLength = channelInfo.channelArn.channelArnLength;
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( channelInfo.channelName.pChannelName != NULL ) )
    {
        if( channelInfo.channelName.channelNameLength > SIGNALING_CONTROLLER_AWS_MAX_CHANNEL_NAME_LENGTH )
        {
            /* Return channel name is longer than expectation. Drop it. */
            LogError( ( "The channel name is too long to store, length=%lu.", channelInfo.channelName.channelNameLength ) );
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_NAME;
        }
        else
        {
            strncpy( pCtx->channelInfo.signalingChannelName, channelInfo.channelName.pChannelName, channelInfo.channelName.channelNameLength );
            pCtx->channelInfo.signalingChannelName[ channelInfo.channelName.channelNameLength ] = '\0';
            pCtx->channelInfo.signalingChannelNameLength = channelInfo.channelName.channelNameLength;
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( channelInfo.messageTtlSeconds != 0U ) )
    {
        pCtx->channelInfo.signalingChannelTtlSeconds = channelInfo.messageTtlSeconds;
    }

    return ret;
}

static SignalingControllerResult_t getSignalingChannelEndpoints( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingAwsRegion_t awsRegion;
    GetSignalingChannelEndpointRequestInfo_t endpointRequestInfo;
    SignalingRequest_t signalRequest;
    HttpRequest_t request;
    HttpResponse_t response;
    SignalingChannelEndpoints_t endpoints;

    // Prepare AWS region
    awsRegion.pAwsRegion = pCtx->credential.region;
    awsRegion.awsRegionLength = pCtx->credential.regionLength;
    // Prepare URL buffer
    signalRequest.pUrl = pCtx->httpUrlBuffer;
    signalRequest.urlLength = SIGNALING_CONTROLLER_MAX_HTTP_URI_LENGTH;
    // Prepare body buffer
    signalRequest.pBody = pCtx->httpBodyBuffer;
    signalRequest.bodyLength = SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH;
    // Create the API url
    endpointRequestInfo.channelArn.pChannelArn = pCtx->channelInfo.signalingChannelARN;
    endpointRequestInfo.channelArn.channelArnLength = pCtx->channelInfo.signalingChannelARNLength;
    endpointRequestInfo.protocols = SIGNALING_PROTOCOL_WEBSOCKET_SECURE | SIGNALING_PROTOCOL_HTTPS;
    endpointRequestInfo.role = SIGNALING_ROLE_MASTER;

    retSignal = Signaling_ConstructGetSignalingChannelEndpointRequest( &awsRegion, &endpointRequestInfo, &signalRequest );

    if( retSignal != SIGNALING_RESULT_OK )
    {
        LogError( ( "Fail to construct get signaling channel endpoint request, return=0x%x", retSignal ) );
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &request, 0, sizeof( HttpRequest_t ) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;
        //request.isFetchingCredential = 0U;

        memset( &response, 0, sizeof( HttpResponse_t ) );
        response.pBuffer = pCtx->httpResponserBuffer;
        response.bufferLength = SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH;

        ret = SignalingController_HttpPerform( pCtx, &request, 0, &response );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retSignal = Signaling_ParseGetSignalingChannelEndpointResponse( response.pBuffer, response.bufferLength, &endpoints );

        if( retSignal != SIGNALING_RESULT_OK )
        {
            LogError( ( "Fail to parse get signaling channel endpoint response, return=0x%x", retSignal ) );
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL;
        }
    }

    // Parse the response
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( ( endpoints.httpsEndpoint.pEndpoint == NULL ) || ( endpoints.httpsEndpoint.endpointLength > SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH ) )
        {
            LogError( ( "No valid HTTPS endpoint found in response" ) );
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_HTTP_ENDPOINT;
        }
        else
        {
            strncpy( pCtx->channelInfo.endpointHttps, endpoints.httpsEndpoint.pEndpoint, endpoints.httpsEndpoint.endpointLength );
            pCtx->channelInfo.endpointHttps[ endpoints.httpsEndpoint.endpointLength ] = '\0';
            pCtx->channelInfo.endpointHttpsLength = endpoints.httpsEndpoint.endpointLength;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( ( endpoints.wssEndpoint.pEndpoint == NULL ) || ( endpoints.wssEndpoint.endpointLength > SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH ) )
        {
            LogError( ( "No valid websocket endpoint found in response" ) );
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_WEBSOCKET_SECURE_ENDPOINT;
        }
        else
        {
            strncpy( pCtx->channelInfo.endpointWebsocketSecure, endpoints.wssEndpoint.pEndpoint, endpoints.wssEndpoint.endpointLength );
            pCtx->channelInfo.endpointWebsocketSecure[ endpoints.wssEndpoint.endpointLength ] = '\0';
            pCtx->channelInfo.endpointWebsocketSecureLength = endpoints.wssEndpoint.endpointLength;
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( endpoints.webrtcEndpoint.pEndpoint != NULL ) )
    {
        if( endpoints.webrtcEndpoint.endpointLength > SIGNALING_CONTROLLER_AWS_MAX_ARN_LENGTH )
        {
            LogError( ( "Length of webRTC endpoint name is too long to store, length=%lu", endpoints.webrtcEndpoint.endpointLength ) );
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_WEBRTC_ENDPOINT;
        }
        else
        {
            strncpy( pCtx->channelInfo.endpointWebrtc, endpoints.webrtcEndpoint.pEndpoint, endpoints.webrtcEndpoint.endpointLength );
            pCtx->channelInfo.endpointWebrtc[ endpoints.webrtcEndpoint.endpointLength ] = '\0';
            pCtx->channelInfo.endpointWebrtcLength = endpoints.webrtcEndpoint.endpointLength;
        }
    }

    return ret;
}

static SignalingControllerResult_t getIceServerList( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingChannelEndpoint_t httpEndpoint;
    GetIceServerConfigRequestInfo_t getIceServerConfigRequestInfo;
    SignalingRequest_t signalRequest;
    HttpRequest_t request;
    HttpResponse_t response;
    SignalingIceServer_t iceServers[ SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT ];
    size_t iceServersNum = SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT;

    // Prepare HTTP endpoint
    httpEndpoint.pEndpoint = pCtx->channelInfo.endpointHttps;
    httpEndpoint.endpointLength = pCtx->channelInfo.endpointHttpsLength;
    // Prepare URL buffer
    signalRequest.pUrl = pCtx->httpUrlBuffer;
    signalRequest.urlLength = SIGNALING_CONTROLLER_MAX_HTTP_URI_LENGTH;
    // Prepare body buffer
    signalRequest.pBody = pCtx->httpBodyBuffer;
    signalRequest.bodyLength = SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH;
    // Create the API url
    getIceServerConfigRequestInfo.channelArn.pChannelArn = pCtx->channelInfo.signalingChannelARN;
    getIceServerConfigRequestInfo.channelArn.channelArnLength = pCtx->channelInfo.signalingChannelARNLength;
    getIceServerConfigRequestInfo.pClientId = "ProducerMaster";
    getIceServerConfigRequestInfo.clientIdLength = strlen( "ProducerMaster" );
    retSignal = Signaling_ConstructGetIceServerConfigRequest( &httpEndpoint, &getIceServerConfigRequestInfo, &signalRequest );

    if( retSignal != SIGNALING_RESULT_OK )
    {
        LogError( ( "Fail to construct get ICE server config request, return=0x%x", retSignal ) );
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_SERVER_LIST_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &request, 0, sizeof( HttpRequest_t ) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;
        //request.isFetchingCredential = 0U;

        memset( &response, 0, sizeof( HttpResponse_t ) );
        response.pBuffer = pCtx->httpResponserBuffer;
        response.bufferLength = SIGNALING_CONTROLLER_MAX_HTTP_BODY_LENGTH;

        ret = SignalingController_HttpPerform( pCtx, &request, 0, &response );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retSignal = Signaling_ParseGetIceServerConfigResponse( response.pBuffer, response.bufferLength, iceServers, &iceServersNum );

        if( retSignal != SIGNALING_RESULT_OK )
        {
            LogError( ( "Fail to parse get ICE server config response, return=0x%x", retSignal ) );
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_GET_SIGNALING_SERVER_LIST_FAIL;
        }
    }

    // Parse the response
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = UpdateIceServerConfigs( pCtx, iceServers, iceServersNum );
    }

    return ret;
}

static SignalingControllerResult_t connectWssEndpoint( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingChannelEndpoint_t wssEndpoint;
    ConnectWssEndpointRequestInfo_t wssEndpointRequestInfo;
    SignalingRequest_t signalRequest;
    WebsocketConnectInfo_t serverInfo;
    NetworkingResult_t networkingResult;
    AwsCredentials_t awsCreds;

    // Prepare WSS endpoint
    wssEndpoint.pEndpoint = pCtx->channelInfo.endpointWebsocketSecure;
    wssEndpoint.endpointLength = pCtx->channelInfo.endpointWebsocketSecureLength;
    // Prepare URL buffer
    signalRequest.pUrl = pCtx->httpUrlBuffer;
    signalRequest.urlLength = SIGNALING_CONTROLLER_MAX_HTTP_URI_LENGTH;
    // Prepare body buffer
    signalRequest.pBody = NULL;
    signalRequest.bodyLength = 0;
    // Create the API url
    memset( &wssEndpointRequestInfo, 0, sizeof( ConnectWssEndpointRequestInfo_t ) );
    wssEndpointRequestInfo.channelArn.pChannelArn = pCtx->channelInfo.signalingChannelARN;
    wssEndpointRequestInfo.channelArn.channelArnLength = pCtx->channelInfo.signalingChannelARNLength;
    wssEndpointRequestInfo.role = SIGNALING_ROLE_MASTER;
    // TODO: for viewer
    // if(wssEndpointRequestInfo.role == SIGNALING_ROLE_VIEWER)
    // {
    //     wssEndpointRequestInfo.pClientId = pCtx->channelInfo.;
    //     wssEndpointRequestInfo.clientIdLength = strlen(pSignalingClient->clientInfo.signalingClientInfo.clientId);
    // }
    retSignal = Signaling_ConstructConnectWssEndpointRequest( &wssEndpoint, &wssEndpointRequestInfo, &signalRequest );

    if( retSignal != SIGNALING_RESULT_OK )
    {
        LogError( ( "Fail to construct connect WSS endpoint request, return=0x%x", retSignal ) );
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        serverInfo.pUrl = signalRequest.pUrl;
        serverInfo.urlLength = signalRequest.urlLength;
        serverInfo.rxCallback = HandleWssMessage;
        serverInfo.pRxCallbackData = pCtx;

        awsCreds.pAccessKeyId = pCtx->credential.accessKeyId;
        awsCreds.accessKeyIdLen = pCtx->credential.accessKeyIdLength;

        awsCreds.pSecretAccessKey = pCtx->credential.secretAccessKey;
        awsCreds.secretAccessKeyLen = pCtx->credential.secretAccessKeyLength;

        awsCreds.pRegion = pCtx->credential.region;
        awsCreds.regionLen = pCtx->credential.regionLength;

        awsCreds.pService = "kinesisvideo";
        awsCreds.serviceLen = strlen( awsCreds.pService );

        awsCreds.pSessionToken = pCtx->credential.sessionToken;
        awsCreds.sessionTokenLength = pCtx->credential.sessionTokenLength;
        awsCreds.expirationSeconds = pCtx->credential.expirationSeconds;

        networkingResult = Networking_WebsocketConnect( &( pCtx->networkingContext ),
                                                        &( serverInfo ),
                                                        &( awsCreds ) );

        if( networkingResult != NETWORKING_RESULT_OK )
        {
            LogError( ( "Fail to connect with WSS endpoint" ) );
            ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL;
        }
    }

    return ret;
}

static SignalingControllerResult_t handleEvent( SignalingControllerContext_t * pCtx,
                                                SignalingControllerEventMessage_t * pEventMsg )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    NetworkingResult_t networkingResult;
    SignalingControllerEventStatus_t callbackEventStatus = SIGNALING_CONTROLLER_EVENT_STATUS_NONE;
    Base64Result_t retBase64;
    WssSendMessage_t wssSendMessage;
    SignalingControllerEventContentSend_t * pEventContentSend;
    SignalingResult_t retSignal;

    switch( pEventMsg->event )
    {
        case SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE:
            /* Allocate the ring buffer to store constructed signaling messages. */
            pEventContentSend = &pEventMsg->eventContent;
            callbackEventStatus = SIGNALING_CONTROLLER_EVENT_STATUS_SENT_FAIL;

            LogDebug( ( "Sending WSS message(%lu): %.*s",
                        pEventContentSend->decodeMessageLength,
                        ( int ) pEventContentSend->decodeMessageLength, pEventContentSend->pDecodeMessage ) );

            /* Then fill the event information, like correlation ID, recipient client ID and base64 encoded message.
             * Note that the message now is not based encoded yet. */
            pCtx->base64BufferLength = SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH;
            retBase64 = Base64_Encode( pEventContentSend->pDecodeMessage, pEventContentSend->decodeMessageLength, pCtx->base64Buffer, &pCtx->base64BufferLength );
            if( retBase64 != BASE64_RESULT_OK )
            {
                ret = SIGNALING_CONTROLLER_RESULT_BASE64_ENCODE_FAIL;
            }

            if( ret == SIGNALING_CONTROLLER_RESULT_OK )
            {
                /* Construct signaling message into ring buffer. */
                memset( &wssSendMessage, 0, sizeof( WssSendMessage_t ) );

                // Prepare the buffer to send
                wssSendMessage.messageType = pEventContentSend->messageType;
                wssSendMessage.pBase64EncodedMessage = pCtx->base64Buffer;
                wssSendMessage.base64EncodedMessageLength = pCtx->base64BufferLength;
                wssSendMessage.pCorrelationId = pEventContentSend->correlationId;
                wssSendMessage.correlationIdLength = pEventContentSend->correlationIdLength;
                wssSendMessage.pRecipientClientId = pEventContentSend->remoteClientId;
                wssSendMessage.recipientClientIdLength = pEventContentSend->remoteClientIdLength;

                /* We must preserve LWS_PRE ahead of buffer for libwebsockets. */
                pCtx->constructedSignalingBufferLength = SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH;
                retSignal = Signaling_ConstructWssMessage( &wssSendMessage, pCtx->constructedSignalingBuffer, &pCtx->constructedSignalingBufferLength );
                if( retSignal != SIGNALING_RESULT_OK )
                {
                    LogError( ( "Fail to construct Wss message, result: %d", retSignal ) );
                    ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_SIGNALING_MSG_FAIL;
                }
            }

            if( ret == SIGNALING_CONTROLLER_RESULT_OK )
            {
                LogVerbose( ( "Constructed WSS message length: %lu, message: \n%.*s", pCtx->constructedSignalingBufferLength,
                              ( int ) pCtx->constructedSignalingBufferLength, pCtx->constructedSignalingBuffer ) );

                /* Finally, sent it to websocket layer. */
                networkingResult = Networking_WebsocketSend( &( pCtx->networkingContext ),
                                                             pCtx->constructedSignalingBuffer,
                                                             pCtx->constructedSignalingBufferLength );

                if( networkingResult != NETWORKING_RESULT_OK )
                {
                    LogError( ( "Fail to construct Wss message, result: %d", retSignal ) );
                    ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_SIGNALING_MSG_FAIL;
                    callbackEventStatus = SIGNALING_CONTROLLER_EVENT_STATUS_SENT_FAIL;
                }
                else
                {
                    callbackEventStatus = SIGNALING_CONTROLLER_EVENT_STATUS_SENT_DONE;
                }
            }
            break;

        default:
            /* Ignore unknown event. */
            LogWarn( ( "Received unknown event %d", pEventMsg->event ) );
            break;
    }

    if( ( pEventMsg->onCompleteCallback != NULL ) && ( callbackEventStatus != SIGNALING_CONTROLLER_EVENT_STATUS_NONE ) )
    {
        pEventMsg->onCompleteCallback( callbackEventStatus, pEventMsg->pOnCompleteCallbackContext );
    }

    return ret;
}

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t * pCtx,
                                                      SignalingControllerCredentialInfo_t * pCredInfo,
                                                      SignalingControllerReceiveMessageCallback receiveMessageCallback,
                                                      void * pReceiveMessageCallbackContext )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;

    if( ( pCtx == NULL ) || ( pCredInfo == NULL ) )
    {
        LogError( ( "Invalid input, pCtx: 0x%p, pCredInfo: 0x%p", pCtx, pCredInfo ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    /* User must provide either access kay pair or role-alias information for AWS cloud access. */
    else if( ( ( pCredInfo->pAccessKeyId == NULL ) || ( pCredInfo->pSecretAccessKey == NULL ) ) &&
             ( ( pCredInfo->pCredEndpoint == NULL ) || ( pCredInfo->pIotThingName == NULL ) ||
               ( pCredInfo->pIotThingRoleAlias == NULL ) ) )
    {
        LogError( ( "Invalid input, pAccessKeyId: 0x%p, pSecretAccessKey: 0x%p, pCredEndpoint: 0x%p, pIotThingName: 0x%p, pIotThingRoleAlias: 0x%p",
                    pCredInfo->pAccessKeyId, pCredInfo->pSecretAccessKey,
                    pCredInfo->pCredEndpoint, pCredInfo->pIotThingName,
                    pCredInfo->pIotThingRoleAlias ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    else if( ( pCredInfo->regionLength > SIGNALING_CONTROLLER_AWS_REGION_MAX_LENGTH ) ||
             ( pCredInfo->channelNameLength > SIGNALING_CONTROLLER_AWS_MAX_CHANNEL_NAME_LENGTH ) ||
             ( pCredInfo->userAgentNameLength > SIGNALING_CONTROLLER_AWS_USER_AGENT_MAX_LENGTH ) )
    {
        LogError( ( "Invalid input, regionLength: %lu, channelNameLength: %lu, userAgentNameLength: %lu",
                    pCredInfo->regionLength, pCredInfo->channelNameLength, pCredInfo->userAgentNameLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    else if( ( pCredInfo->accessKeyIdLength > SIGNALING_CONTROLLER_ACCESS_KEY_ID_MAX_LENGTH ) ||
             ( pCredInfo->secretAccessKeyLength > SIGNALING_CONTROLLER_SECRET_ACCESS_KEY_MAX_LENGTH ) ||
             ( pCredInfo->sessionTokenLength > SIGNALING_CONTROLLER_AWS_MAX_SESSION_TOKEN_LENGTH ) )
    {
        LogError( ( "Invalid input, accessKeyIdLength: %lu, secretAccessKeyLength: %lu, sessionTokenLength: %lu",
                    pCredInfo->accessKeyIdLength, pCredInfo->secretAccessKeyLength, pCredInfo->sessionTokenLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    else if( ( pCredInfo->caCertPathLength > SIGNALING_CONTROLLER_MAX_PATH_LENGTH ) ||
             ( pCredInfo->caCertPemSize > SIGNALING_CONTROLLER_CERTIFICATE_MAX_LENGTH ) )
    {
        LogError( ( "Invalid input, caCertPathLength: %lu, caCertPemSize: %lu",
                    pCredInfo->caCertPathLength, pCredInfo->caCertPemSize ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    else if( ( pCredInfo->credEndpointLength > SIGNALING_CONTROLLER_AWS_CRED_ENDPOINT_MAX_LENGTH ) ||
             ( pCredInfo->iotThingNameLength > SIGNALING_CONTROLLER_AWS_IOT_THING_NAME_MAX_LENGTH ) ||
             ( pCredInfo->iotThingRoleAliasLength > SIGNALING_CONTROLLER_AWS_IOT_ROLE_ALIAS_MAX_LENGTH ) )
    {
        LogError( ( "Invalid input, credEndpointLength: %lu, iotThingNameLength: %lu, iotThingRoleAliasLength: %lu",
                    pCredInfo->credEndpointLength, pCredInfo->iotThingNameLength, pCredInfo->iotThingRoleAliasLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    else if( ( pCredInfo->iotThingCertPathLength > SIGNALING_CONTROLLER_MAX_PATH_LENGTH ) ||
             ( pCredInfo->iotThingPrivateKeyPathLength > SIGNALING_CONTROLLER_MAX_PATH_LENGTH ) )
    {
        LogError( ( "Invalid input, iotThingCertPathLength: %lu, iotThingPrivateKeyPathLength: %lu",
                    pCredInfo->iotThingCertPathLength, pCredInfo->iotThingPrivateKeyPathLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    else
    {
        /* Empty else marker. */
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Initialize signaling controller context. */
        memset( pCtx, 0, sizeof( SignalingControllerContext_t ) );
        memcpy( pCtx->credential.region, pCredInfo->pRegion, pCredInfo->regionLength );
        pCtx->credential.regionLength = pCredInfo->regionLength;
        pCtx->credential.region[ pCtx->credential.regionLength ] = '\0';

        memcpy( pCtx->credential.channelName, pCredInfo->pChannelName, pCredInfo->channelNameLength );
        pCtx->credential.channelNameLength = pCredInfo->channelNameLength;
        pCtx->credential.channelName[ pCtx->credential.channelNameLength ] = '\0';

        memcpy( pCtx->credential.userAgentName, pCredInfo->pUserAgentName, pCredInfo->userAgentNameLength );
        pCtx->credential.userAgentNameLength = pCredInfo->userAgentNameLength;
        pCtx->credential.userAgentName[ pCtx->credential.userAgentNameLength ] = '\0';

        memcpy( pCtx->credential.accessKeyId, pCredInfo->pAccessKeyId, pCredInfo->accessKeyIdLength );
        pCtx->credential.accessKeyIdLength = pCredInfo->accessKeyIdLength;
        pCtx->credential.accessKeyId[ pCtx->credential.accessKeyIdLength ] = '\0';
        memcpy( pCtx->credential.secretAccessKey, pCredInfo->pSecretAccessKey, pCredInfo->secretAccessKeyLength );
        pCtx->credential.secretAccessKeyLength = pCredInfo->secretAccessKeyLength;
        pCtx->credential.secretAccessKey[ pCtx->credential.secretAccessKeyLength ] = '\0';
        memcpy( pCtx->credential.sessionToken, pCredInfo->pSessionToken, pCredInfo->sessionTokenLength );
        pCtx->credential.sessionTokenLength = pCredInfo->sessionTokenLength;
        pCtx->credential.sessionToken[ pCtx->credential.sessionTokenLength ] = '\0';

        memcpy( pCtx->credential.caCertPath, pCredInfo->pCaCertPath, pCredInfo->caCertPathLength );
        pCtx->credential.caCertPathLength = pCredInfo->caCertPathLength;
        pCtx->credential.caCertPath[ pCtx->credential.caCertPathLength ] = '\0';

        memcpy( pCtx->credential.caCertPem, pCredInfo->pCaCertPem, pCredInfo->caCertPemSize );
        pCtx->credential.caCertPemSize = pCredInfo->caCertPemSize;
        pCtx->credential.caCertPem[ pCtx->credential.caCertPemSize ] = '\0';

        memcpy( pCtx->credential.credEndpoint, pCredInfo->pCredEndpoint, pCredInfo->credEndpointLength );
        pCtx->credential.credEndpointLength = pCredInfo->credEndpointLength;
        pCtx->credential.credEndpoint[ pCtx->credential.credEndpointLength ] = '\0';

        memcpy( pCtx->credential.iotThingName, pCredInfo->pIotThingName, pCredInfo->iotThingNameLength );
        pCtx->credential.iotThingNameLength = pCredInfo->iotThingNameLength;
        pCtx->credential.iotThingName[ pCtx->credential.iotThingNameLength ] = '\0';

        memcpy( pCtx->credential.iotThingRoleAlias, pCredInfo->pIotThingRoleAlias, pCredInfo->iotThingRoleAliasLength );
        pCtx->credential.iotThingRoleAliasLength = pCredInfo->iotThingRoleAliasLength;
        pCtx->credential.iotThingRoleAlias[ pCtx->credential.iotThingRoleAliasLength ] = '\0';

        memcpy( pCtx->credential.iotThingCertPath, pCredInfo->pIotThingCertPath, pCredInfo->iotThingCertPathLength );
        pCtx->credential.iotThingCertPathLength = pCredInfo->iotThingCertPathLength;
        pCtx->credential.iotThingCertPath[ pCtx->credential.iotThingCertPathLength ] = '\0';

        memcpy( pCtx->credential.iotThingPrivateKeyPath, pCredInfo->pIotThingPrivateKeyPath, pCredInfo->iotThingPrivateKeyPathLength );
        pCtx->credential.iotThingPrivateKeyPathLength = pCredInfo->iotThingPrivateKeyPathLength;
        pCtx->credential.iotThingPrivateKeyPath[ pCtx->credential.iotThingPrivateKeyPathLength ] = '\0';

        pCtx->receiveMessageCallback = receiveMessageCallback;
        pCtx->pReceiveMessageCallbackContext = pReceiveMessageCallbackContext;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        SSLCredentials_t sslCreds;
        NetworkingResult_t networkingResult;

        sslCreds.pCaCertPath = pCtx->credential.caCertPath;
        sslCreds.pDeviceCertPath = pCredInfo->iotThingCertPathLength == 0 ? NULL : pCtx->credential.iotThingCertPath;
        sslCreds.pDeviceKeyPath = pCredInfo->iotThingPrivateKeyPathLength == 0 ? NULL : pCtx->credential.iotThingPrivateKeyPath;

        networkingResult = Networking_Init( &( pCtx->networkingContext ),
                                            &( sslCreds ) );

        if( networkingResult != NETWORKING_RESULT_OK )
        {
            LogError( ( "Failed to initialize networking!") );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    return ret;
}

void SignalingController_Deinit( SignalingControllerContext_t * pCtx )
{
    ( void ) pCtx;
}

SignalingControllerResult_t SignalingController_IceServerReconnection( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;

    if( pCtx == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Change the State to Describe state and attempt Re-connection. */
        ret = getIceServerList( pCtx );
    }
    else
    {
        LogWarn( ( " Fetching ICE Server List Unsuccessful. " ) );
    }

    return ret;
}

SignalingControllerResult_t SignalingController_ConnectServers( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingControllerIceServerConfig_t * pIceServerConfigs;
    size_t iceServerConfigsCount;
    uint64_t currentTimeSeconds;
    uint8_t needFetchCredential = 0U;

    /* Check input parameters. */
    if( pCtx == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    /* Get security token. */
    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) &&
        ( pCtx->credential.iotThingRoleAliasLength > 0 ) )
    {
        currentTimeSeconds = NetworkingUtils_GetCurrentTimeSec( NULL );

        if( ( pCtx->credential.expirationSeconds == 0 ) ||
            ( currentTimeSeconds >= pCtx->credential.expirationSeconds - SIGNALING_CONTROLLER_FETCH_SESSION_TOKEN_GRACE_PERIOD_SEC ) )
        {
            needFetchCredential = 1U;
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( needFetchCredential != 0U ) )
    {
        Metric_StartEvent( METRIC_EVENT_SIGNALING_GET_CREDENTIALS );
        ret = FetchTemporaryCredentials( pCtx,
                                         &pCtx->credential );
        Metric_EndEvent( METRIC_EVENT_SIGNALING_GET_CREDENTIALS );
    }

    /* Execute describe channel if no channel ARN. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        Metric_StartEvent( METRIC_EVENT_SIGNALING_DESCRIBE_CHANNEL );
        ret = describeSignalingChannel( pCtx );
        Metric_EndEvent( METRIC_EVENT_SIGNALING_DESCRIBE_CHANNEL );
    }

    /* Query signaling channel endpoints with channel ARN. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        Metric_StartEvent( METRIC_EVENT_SIGNALING_GET_ENDPOINTS );
        ret = getSignalingChannelEndpoints( pCtx );
        Metric_EndEvent( METRIC_EVENT_SIGNALING_GET_ENDPOINTS );
    }

    /* Query ICE server list with HTTPS endpoint. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = SignalingController_QueryIceServerConfigs( pCtx, &pIceServerConfigs, &iceServerConfigsCount );
    }

    /* Connect websocket secure endpoint. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        Metric_StartEvent( METRIC_EVENT_SIGNALING_CONNECT_WSS_SERVER );
        ret = connectWssEndpoint( pCtx );
        Metric_EndEvent( METRIC_EVENT_SIGNALING_CONNECT_WSS_SERVER );
    }

    /* Print metric. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        printMetrics( pCtx );
    }

    return ret;
}

SignalingControllerResult_t SignalingController_ProcessLoop( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    NetworkingResult_t result = NETWORKING_RESULT_OK;

    for( ;; )
    {
        ret = SignalingController_ConnectServers( pCtx );

        if( ret == SIGNALING_CONTROLLER_RESULT_OK )
        {
            /* Clear previous result. */
            result = NETWORKING_RESULT_OK;
        }
        else
        {
            LogWarn( ( "Fail to connect signaling server, result: %d", ret ) );
        }

        while( result == NETWORKING_RESULT_OK )
        {
            result = Networking_WebsocketSignal( &( pCtx->networkingContext ) );
        }
    }

    return ret;
}

SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t * pCtx,
                                                             SignalingControllerEventMessage_t * pEventMsg )
{
    return handleEvent( pCtx, pEventMsg );
}

SignalingControllerResult_t SignalingController_QueryIceServerConfigs( SignalingControllerContext_t * pCtx,
                                                                       SignalingControllerIceServerConfig_t ** ppIceServerConfigs,
                                                                       size_t * pIceServerConfigsCount )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    uint64_t currentTimeSec;

    if( ( pCtx == NULL ) || ( ppIceServerConfigs == NULL ) || ( pIceServerConfigsCount == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        currentTimeSec = NetworkingUtils_GetCurrentTimeSec( NULL );

        if( ( pCtx->iceServerConfigsCount == 0 ) || ( pCtx->iceServerConfigExpirationSec < currentTimeSec ) )
        {
            LogInfo( ( "Ice server configs expired, Starting Refresing Configs." ) );

            Metric_StartEvent( METRIC_EVENT_SIGNALING_GET_ICE_SERVER_LIST );
            ret = getIceServerList( pCtx );
            Metric_EndEvent( METRIC_EVENT_SIGNALING_GET_ICE_SERVER_LIST );
        }
        *ppIceServerConfigs = pCtx->iceServerConfigs;
        *pIceServerConfigsCount = pCtx->iceServerConfigsCount;
    }

    return ret;
}

SignalingControllerResult_t SignalingController_GetSdpContentFromEventMsg( const char * pEventMessage,
                                                                           size_t eventMessageLength,
                                                                           uint8_t isSdpOffer,
                                                                           const char ** ppSdpMessage,
                                                                           size_t * pSdpMessageLength )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    JSONStatus_t jsonResult;
    size_t start = 0, next = 0;
    JSONPair_t pair = { 0 };
    uint8_t isContentFound = 0;
    const char * pTargetTypeValue = isSdpOffer == 1 ? SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_OFFER : SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_ANSWER;

    if( ( pEventMessage == NULL ) ||
        ( ppSdpMessage == NULL ) ||
        ( pSdpMessageLength == NULL ) )
    {
        LogError( ( "Invalid input, pEventMessage: %p, ppSdpMessage: %p, pSdpMessageLength: %p", pEventMessage, ppSdpMessage, pSdpMessageLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        jsonResult = JSON_Validate( pEventMessage, eventMessageLength );

        if( jsonResult != JSONSuccess )
        {
            LogWarn( ( "Input message is not valid JSON message, result: %d, message(%lu): %.*s",
                       jsonResult,
                       eventMessageLength,
                       ( int ) eventMessageLength,
                       pEventMessage ) );
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_JSON;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Check if it's SDP offer. */
        jsonResult = JSON_Iterate( pEventMessage, eventMessageLength, &start, &next, &pair );

        while( jsonResult == JSONSuccess )
        {
            if( ( strncmp( pair.key, SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_KEY, pair.keyLength ) == 0 ) &&
                ( strncmp( pair.value, pTargetTypeValue, pair.valueLength ) != 0 ) )
            {
                /* It's not expected SDP offer message. */
                LogWarn( ( "Message type \"%.*s\" is not SDP target type \"%s\"",
                           ( int ) pair.valueLength, pair.value,
                           pTargetTypeValue ) );
                ret = SIGNALING_CONTROLLER_RESULT_SDP_NOT_TARGET_TYPE;
                break;
            }
            else if( strncmp( pair.key, SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_CONTENT_KEY, pair.keyLength ) == 0 )
            {
                *ppSdpMessage = pair.value;
                *pSdpMessageLength = pair.valueLength;
                isContentFound = 1;
                break;
            }
            else
            {
                /* Skip unknown attributes. */
            }

            jsonResult = JSON_Iterate( pEventMessage, eventMessageLength, &start, &next, &pair );
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && !isContentFound )
    {
        LogWarn( ( "No target content found in event message, result: %d, SDP target type \"%s\", message(%lu): %.*s",
                   jsonResult,
                   pTargetTypeValue,
                   eventMessageLength,
                   ( int ) eventMessageLength,
                   pEventMessage ) );
        ret = SIGNALING_CONTROLLER_RESULT_SDP_NOT_TARGET_TYPE;
    }

    return ret;
}

SignalingControllerResult_t SignalingController_DeserializeSdpContentNewline( const char * pSdpMessage,
                                                                              size_t sdpMessageLength,
                                                                              char * pFormalSdpMessage,
                                                                              size_t * pFormalSdpMessageLength )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    const char * pCurSdp = pSdpMessage, * pNext = NULL;
    char * pCurOutput = NULL;
    size_t lineLength = 0, outputLength = 0;

    if( ( pSdpMessage == NULL ) ||
        ( pFormalSdpMessage == NULL ) ||
        ( pFormalSdpMessageLength == NULL ) )
    {
        LogError( ( "Invalid input, pSdpMessage: %p, pFormalSdpMessage: %p, pFormalSdpMessageLength: %p", pSdpMessage, pFormalSdpMessage, pFormalSdpMessageLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        pCurOutput = pFormalSdpMessage;

        while( ( pNext = strstr( pCurSdp, SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_NEWLINE_ENDING ) ) != NULL )
        {
            lineLength = pNext - pCurSdp;

            if( ( lineLength >= 2 ) &&
                ( pCurSdp[ lineLength - 2 ] == '\\' ) && ( pCurSdp[ lineLength - 1 ] == 'r' ) )
            {
                lineLength -= 2;
            }

            if( *pFormalSdpMessageLength < outputLength + lineLength + 2 )
            {
                LogWarn( ( "Buffer space is not enough to store formal SDP message, buffer size: %lu, SDP message(%lu): %.*s",
                           *pFormalSdpMessageLength,
                           sdpMessageLength,
                           ( int ) sdpMessageLength,
                           pSdpMessage ) );
                ret = SIGNALING_CONTROLLER_RESULT_SDP_BUFFER_TOO_SMALL;
                break;
            }

            memcpy( pCurOutput, pCurSdp, lineLength );
            pCurOutput += lineLength;
            *pCurOutput++ = '\r';
            *pCurOutput++ = '\n';
            outputLength += lineLength + 2;

            pCurSdp = pNext + 2;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        *pFormalSdpMessageLength = outputLength;
    }

    return ret;
}

SignalingControllerResult_t SignalingController_SerializeSdpContentNewline( const char * pSdpMessage,
                                                                            size_t sdpMessageLength,
                                                                            char * pEventSdpMessage,
                                                                            size_t * pEventSdpMessageLength )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    const char * pCurSdp = pSdpMessage, * pNext, * pTail;
    char * pCurOutput = pEventSdpMessage;
    size_t lineLength, outputLength = 0;
    int writtenLength;

    if( ( pSdpMessage == NULL ) ||
        ( pEventSdpMessage == NULL ) ||
        ( pEventSdpMessageLength == NULL ) )
    {
        LogError( ( "Invalid input, pSdpMessage: %p, pEventSdpMessage: %p, pEventSdpMessageLength: %p", pSdpMessage, pEventSdpMessage, pEventSdpMessageLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        pTail = pSdpMessage + sdpMessageLength;

        while( ( pNext = memchr( pCurSdp, '\n', pTail - pCurSdp ) ) != NULL )
        {
            lineLength = pNext - pCurSdp;

            if( ( lineLength > 0 ) &&
                ( pCurSdp[ lineLength - 1 ] == '\r' ) )
            {
                lineLength--;
            }
            else
            {
                /* do nothing, coverity happy. */
            }

            if( *pEventSdpMessageLength < outputLength + lineLength + 4 )
            {
                LogError( ( "The output buffer length(%lu) is too small to store serialized %lu bytes message.",
                            *pEventSdpMessageLength,
                            sdpMessageLength ) );
                ret = SIGNALING_CONTROLLER_RESULT_SDP_BUFFER_TOO_SMALL;
                break;
            }

            writtenLength = snprintf( pCurOutput, *pEventSdpMessageLength - outputLength, "%.*s\\r\\n",
                                      ( int ) lineLength,
                                      pCurSdp );
            if( writtenLength < 0 )
            {
                ret = SIGNALING_CONTROLLER_RESULT_SDP_SNPRINTF_FAIL;
                LogError( ( "snprintf returns fail %d", writtenLength ) );
                break;
            }
            else
            {
                outputLength += lineLength + 4;
                pCurOutput += lineLength + 4;
            }

            pCurSdp = pNext + 1;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( pTail > pCurSdp )
        {
            /* Copy the ending string. */
            lineLength = pTail - pCurSdp;
            memcpy( pCurOutput, pCurSdp, lineLength );

            outputLength += lineLength;
            pCurOutput += lineLength;
            pCurSdp += lineLength;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        *pEventSdpMessageLength = outputLength;
    }

    return ret;
}
