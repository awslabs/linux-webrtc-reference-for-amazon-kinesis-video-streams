#include <string.h>
#include "logging.h"
#include "signaling_controller.h"
#include "signaling_api.h"
#include "http.h"
#include "websocket.h"
#include "base64.h"
#include "metric.h"
#include "core_json.h"
#include "networking_utils.h"

#ifndef MIN
#define MIN( a,b ) ( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#endif

#if ( defined( SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS ) && SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS )
    #include "libwebsockets.h"
    #include "networkingLibwebsockets.h"
#elif ( defined( SIGNALING_CONTROLLER_USING_COREHTTP ) && SIGNALING_CONTROLLER_USING_COREHTTP ) /* defined( SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS ) && SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS */
    #include "core_http_helper.h"
#endif /* defined( SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS ) && SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS */

#if ( defined( SIGNALING_CONTROLLER_USING_WSLAY ) && SIGNALING_CONTROLLER_USING_WSLAY )
    #include "wslay_helper.h"
#endif /* defined( SIGNALING_CONTROLLER_USING_WSLAY ) && SIGNALING_CONTROLLER_USING_WSLAY */
#define SIGNALING_CONTROLLER_MESSAGE_QUEUE_NAME "/WebrtcApplicationSignalingController"

#define MAX_QUEUE_MSG_NUM ( 10 )
#define WEBSOCKET_ENDPOINT_PORT ( 443U )
#define HTTPS_PERFORM_RETRY_TIMES ( 5U )

#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_KEY "type"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_OFFER "offer"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_ANSWER "answer"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_CONTENT_KEY "sdp"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_NEWLINE_ENDING "\\n"

static SignalingControllerResult_t updateIceServerConfigs( SignalingControllerContext_t * pCtx,
                                                           SignalingIceServer_t * pIceServerList,
                                                           size_t iceServerListNum );

static WebsocketResult_t handleWssMessage( char * pMessage,
                                           size_t messageLength,
                                           void * pUserContext )
{
    WebsocketResult_t ret = WEBSOCKET_RESULT_OK;
    SignalingResult_t retSignal;
    WssRecvMessage_t wssRecvMessage;
    SignalingControllerContext_t * pCtx = ( SignalingControllerContext_t * ) pUserContext;
    SignalingControllerReceiveEvent_t receiveEvent;
    Base64Result_t retBase64;

    if( ( pMessage == NULL ) || ( messageLength == 0 ) )
    {
        LogDebug( ( "Received empty signaling message" ) );
        ret = WEBSOCKET_RESULT_BAD_PARAMETER;
    }

    if( ret == WEBSOCKET_RESULT_OK )
    {
        // Parse the response
        retSignal = Signaling_ParseWssRecvMessage( pMessage, ( size_t ) messageLength, &wssRecvMessage );
        if( retSignal != SIGNALING_RESULT_OK )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_UNKNOWN_MESSAGE;
        }
    }

    /* Decode base64 payload. */
    if( ret == WEBSOCKET_RESULT_OK )
    {
        pCtx->base64BufferLength = SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH;
        retBase64 = Base64_Decode( wssRecvMessage.pBase64EncodedPayload, wssRecvMessage.base64EncodedPayloadLength, pCtx->base64Buffer, &pCtx->base64BufferLength );

        if( retBase64 != BASE64_RESULT_OK )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_BASE64_DECODE_FAIL;
        }
    }

    if( ret == WEBSOCKET_RESULT_OK )
    {
        memset( &receiveEvent, 0, sizeof( SignalingControllerReceiveEvent_t ) );
        receiveEvent.pRemoteClientId = wssRecvMessage.pSenderClientId;
        receiveEvent.remoteClientIdLength = wssRecvMessage.senderClientIdLength;
        receiveEvent.pCorrelationId = wssRecvMessage.statusResponse.pCorrelationId;
        receiveEvent.correlationIdLength = wssRecvMessage.statusResponse.correlationIdLength;
        receiveEvent.messageType = wssRecvMessage.messageType;
        receiveEvent.pDecodeMessage = pCtx->base64Buffer;
        receiveEvent.decodeMessageLength = pCtx->base64BufferLength;

        if( pCtx->receiveMessageCallback != NULL )
        {
            pCtx->receiveMessageCallback( &receiveEvent, pCtx->pReceiveMessageCallbackContext );
        }
    }

    return ret;
}

#if ( defined( SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS ) && SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS )

static SignalingControllerResult_t SignalingController_HttpInit( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    HttpResult_t retHttp;
    NetworkingLibwebsocketsCredentials_t libwebsocketsCred;

    libwebsocketsCred.pUserAgent = pCtx->credential.pUserAgentName;
    libwebsocketsCred.userAgentLength = pCtx->credential.userAgentNameLength;
    libwebsocketsCred.pRegion = pCtx->credential.pRegion;
    libwebsocketsCred.regionLength = pCtx->credential.regionLength;
    libwebsocketsCred.pAccessKeyId = pCtx->credential.pAccessKeyId;
    libwebsocketsCred.accessKeyIdLength = pCtx->credential.accessKeyIdLength;
    libwebsocketsCred.pSecretAccessKey = pCtx->credential.pSecretAccessKey;
    libwebsocketsCred.secretAccessKeyLength = pCtx->credential.secretAccessKeyLength;
    libwebsocketsCred.pCaCertPath = pCtx->credential.pCaCertPath;

    retHttp = Http_Init( &libwebsocketsCred );

    if( retHttp != HTTP_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_HTTP_INIT_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t SignalingController_WebsocketInit( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    WebsocketResult_t retWebsocket;
    NetworkingLibwebsocketsCredentials_t libwebsocketsCred;

    libwebsocketsCred.pUserAgent = pCtx->credential.pUserAgentName;
    libwebsocketsCred.userAgentLength = pCtx->credential.userAgentNameLength;
    libwebsocketsCred.pRegion = pCtx->credential.pRegion;
    libwebsocketsCred.regionLength = pCtx->credential.regionLength;
    libwebsocketsCred.pAccessKeyId = pCtx->credential.pAccessKeyId;
    libwebsocketsCred.accessKeyIdLength = pCtx->credential.accessKeyIdLength;
    libwebsocketsCred.pSecretAccessKey = pCtx->credential.pSecretAccessKey;
    libwebsocketsCred.secretAccessKeyLength = pCtx->credential.secretAccessKeyLength;
    libwebsocketsCred.pCaCertPath = pCtx->credential.pCaCertPath;

    retWebsocket = Websocket_Init( &libwebsocketsCred, handleWssMessage, pCtx );

    if( retWebsocket != HTTP_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_WEBSOCKET_INIT_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t SignalingController_HttpPerform( SignalingControllerContext_t * pCtx,
                                                                    HttpRequest_t * pRequest,
                                                                    size_t timeoutMs,
                                                                    HttpResponse_t * pResponse )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    HttpResult_t retHttp;
    int i;

    for( i = 0; i < HTTPS_PERFORM_RETRY_TIMES; i++ )
    {
        retHttp = Http_Send( pRequest, timeoutMs, pResponse );

        if( retHttp == HTTP_RESULT_OK )
        {
            break;
        }
    }

    if( retHttp != HTTP_RESULT_OK )
    {
        LogError( ( "Http_Send fails with return 0x%x", retHttp ) );
        ret = SIGNALING_CONTROLLER_RESULT_HTTP_PERFORM_REQUEST_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t SignalingController_WebsocketConnect( WebsocketServerInfo_t * pServerInfo )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    WebsocketResult_t retWebsocket;
    int i;

    for( i = 0; i < HTTPS_PERFORM_RETRY_TIMES; i++ )
    {
        retWebsocket = Websocket_Connect( pServerInfo );

        if( retWebsocket == WEBSOCKET_RESULT_OK )
        {
            break;
        }
    }


    if( retWebsocket != WEBSOCKET_RESULT_OK )
    {
        LogError( ( "Fail to connect url: %.*s:%u, return=0x%x",
                    ( int ) pServerInfo->urlLength, pServerInfo->pUrl, pServerInfo->port, retWebsocket ) );
        ret = SIGNALING_CONTROLLER_RESULT_WSS_CONNECT_FAIL;
    }

    return ret;
}
#elif ( defined( SIGNALING_CONTROLLER_USING_COREHTTP ) && SIGNALING_CONTROLLER_USING_COREHTTP ) /* defined( SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS ) && SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS */
static SignalingControllerResult_t SignalingController_HttpInit( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    HttpResult_t retHttp;
    NetworkingCorehttpCredentials_t coreHttpCred;

    coreHttpCred.pUserAgent = pCtx->credential.pUserAgentName;
    coreHttpCred.userAgentLength = pCtx->credential.userAgentNameLength;
    coreHttpCred.pRegion = pCtx->credential.pRegion;
    coreHttpCred.regionLength = pCtx->credential.regionLength;
    coreHttpCred.pAccessKeyId = pCtx->credential.pAccessKeyId;
    coreHttpCred.accessKeyIdLength = pCtx->credential.accessKeyIdLength;
    coreHttpCred.pSecretAccessKey = pCtx->credential.pSecretAccessKey;
    coreHttpCred.secretAccessKeyLength = pCtx->credential.secretAccessKeyLength;
    coreHttpCred.pCaCertPath = pCtx->credential.pCaCertPath;
    coreHttpCred.pRootCa = ( uint8_t * ) pCtx->credential.pCaCertPem;
    coreHttpCred.rootCaSize = pCtx->credential.caCertPemSize;

    retHttp = Http_Init( &coreHttpCred );

    if( retHttp != HTTP_RESULT_OK )
    {
        LogError( ( "Http_Init fails with return 0x%x", retHttp ) );
        ret = SIGNALING_CONTROLLER_RESULT_HTTP_INIT_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t SignalingController_HttpPerform( SignalingControllerContext_t * pCtx,
                                                                    HttpRequest_t * pRequest,
                                                                    size_t timeoutMs,
                                                                    HttpResponse_t * pResponse )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    HttpResult_t retHttp;
    int i;

    for( i = 0; i < HTTPS_PERFORM_RETRY_TIMES; i++ )
    {
        retHttp = Http_Send( pRequest, timeoutMs, pResponse );

        if( retHttp == HTTP_RESULT_OK )
        {
            break;
        }
    }

    if( retHttp != HTTP_RESULT_OK )
    {
        LogError( ( "Http_Send fails with return 0x%x", retHttp ) );
        ret = SIGNALING_CONTROLLER_RESULT_HTTP_PERFORM_REQUEST_FAIL;
    }

    return ret;
}
#endif /* defined( SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS ) && SIGNALING_CONTROLLER_USING_LIBWEBSOCKETS */

#if ( defined( SIGNALING_CONTROLLER_USING_WSLAY ) && SIGNALING_CONTROLLER_USING_WSLAY )
static SignalingControllerResult_t SignalingController_WebsocketInit( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    WebsocketResult_t retWebsocket;
    NetworkingWslayCredentials_t credential;

    credential.pUserAgent = pCtx->credential.pUserAgentName;
    credential.userAgentLength = pCtx->credential.userAgentNameLength;
    credential.pRegion = pCtx->credential.pRegion;
    credential.regionLength = pCtx->credential.regionLength;
    credential.pAccessKeyId = pCtx->credential.pAccessKeyId;
    credential.accessKeyIdLength = pCtx->credential.accessKeyIdLength;
    credential.pSecretAccessKey = pCtx->credential.pSecretAccessKey;
    credential.secretAccessKeyLength = pCtx->credential.secretAccessKeyLength;
    credential.pCaCertPath = pCtx->credential.pCaCertPath;
    credential.pRootCa = ( uint8_t * ) pCtx->credential.pCaCertPem;
    credential.rootCaSize = pCtx->credential.caCertPemSize;

    retWebsocket = Websocket_Init( &credential, handleWssMessage, pCtx );

    if( retWebsocket != WEBSOCKET_RESULT_OK )
    {
        LogError( ( "Fail to initialize websocket library, return=0x%x", retWebsocket ) );
        ret = SIGNALING_CONTROLLER_RESULT_WEBSOCKET_INIT_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t SignalingController_WebsocketConnect( WebsocketServerInfo_t * pServerInfo )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    WebsocketResult_t retWebsocket;
    int i;

    for( i = 0; i < HTTPS_PERFORM_RETRY_TIMES; i++ )
    {
        retWebsocket = Websocket_Connect( pServerInfo );

        if( retWebsocket == WEBSOCKET_RESULT_OK )
        {
            break;
        }
    }

    if( retWebsocket != WEBSOCKET_RESULT_OK )
    {
        LogError( ( "Fail to connect url: %.*s:%u, return=0x%x",
                    ( int ) pServerInfo->urlLength, pServerInfo->pUrl, pServerInfo->port, retWebsocket ) );
        ret = SIGNALING_CONTROLLER_RESULT_WSS_CONNECT_FAIL;
    }

    return ret;
}
#endif /* defined( SIGNALING_CONTROLLER_USING_WSLAY ) && SIGNALING_CONTROLLER_USING_WSLAY */

static void printMetrics( SignalingControllerContext_t * pCtx )
{
    uint8_t i, j;

    /* channel info */
    LogInfo( ( "======================================== Channel Info ========================================" ) );
    LogInfo( ( "Signaling Channel Name: %s", pCtx->channelInfo.signalingChannelName ) );
    LogInfo( ( "Signaling Channel ARN: %s", pCtx->channelInfo.signalingChannelARN ) );
    LogInfo( ( "Signaling Channel TTL (seconds): %lu", pCtx->channelInfo.signalingChannelTtlSeconds ) );
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
        LogInfo( ( "    TTL (secodns): %lu", pCtx->iceServerConfigs[i].ttlSeconds ) );
        LogInfo( ( "    User Name: %s", pCtx->iceServerConfigs[i].userName ) );
        LogInfo( ( "    Password: %s", pCtx->iceServerConfigs[i].password ) );
        LogInfo( ( "    URI Count: %u", pCtx->iceServerConfigs[i].uriCount ) );
        for( j = 0; j < pCtx->iceServerConfigs[i].uriCount; j++ )
        {
            LogInfo( ( "        URI: %s", pCtx->iceServerConfigs[i].uris[j] ) );
        }
    }
}

static SignalingControllerResult_t updateIceServerConfigs( SignalingControllerContext_t * pCtx,
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
                LogError( ( "The length of user name of ice server is too long to store, length=%d", pIceServerList[i].userNameLength ) );
                ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_USERNAME;
                break;
            }
            else if( pIceServerList[i].passwordLength >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_PASSWORD_LENGTH )
            {
                LogError( ( "The length of password of ice server is too long to store, length=%d", pIceServerList[i].passwordLength ) );
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
                    LogError( ( "The length of URI of ice server is too long to store, length=%d", pIceServerList[i].urisLength[j] ) );
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
    awsRegion.pAwsRegion = pCtx->credential.pRegion;
    awsRegion.awsRegionLength = pCtx->credential.regionLength;
    // Prepare channel name
    channelName.pChannelName = pCtx->credential.pChannelName;
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
            LogError( ( "Fail to parse describe signaling channel response, return=0x%x, response(%u): %.*s", retSignal, response.bufferLength,
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
            LogError( ( "The channel name is too long to store, length=%d.", channelInfo.channelName.channelNameLength ) );
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
    awsRegion.pAwsRegion = pCtx->credential.pRegion;
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
            LogError( ( "Length of webRTC endpoint name is too long to store, length=%d", endpoints.webrtcEndpoint.endpointLength ) );
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
        ret = updateIceServerConfigs( pCtx, iceServers, iceServersNum );
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
    WebsocketServerInfo_t serverInfo;

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
        serverInfo.port = WEBSOCKET_ENDPOINT_PORT;
        ret = SignalingController_WebsocketConnect( &serverInfo );

        if( ret != SIGNALING_CONTROLLER_RESULT_OK )
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
    WebsocketResult_t websocketRet;
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

            LogDebug( ( "Sending WSS message(%u): %.*s",
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
                LogVerbose( ( "Constructed WSS message length: %u, message: \n%.*s", pCtx->constructedSignalingBufferLength,
                              ( int ) pCtx->constructedSignalingBufferLength, pCtx->constructedSignalingBuffer ) );

                /* Finally, sent it to websocket layer. */
                websocketRet = Websocket_Send( pCtx->constructedSignalingBuffer, pCtx->constructedSignalingBufferLength );
                if( websocketRet != WEBSOCKET_RESULT_OK )
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
                                                      SignalingControllerCredential_t * pCred,
                                                      SignalingControllerReceiveMessageCallback receiveMessageCallback,
                                                      void * pReceiveMessageCallbackContext )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    MessageQueueResult_t retMessageQueue;

    if( ( pCtx == NULL ) || ( pCred == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    else if( ( pCred->pAccessKeyId == NULL ) || ( pCred->pSecretAccessKey == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Initialize signaling controller context. */
        memset( pCtx, 0, sizeof( SignalingControllerContext_t ) );
        pCtx->credential.pRegion = pCred->pRegion;
        pCtx->credential.regionLength = pCred->regionLength;

        pCtx->credential.pChannelName = pCred->pChannelName;
        pCtx->credential.channelNameLength = pCred->channelNameLength;

        pCtx->credential.pUserAgentName = pCred->pUserAgentName;
        pCtx->credential.userAgentNameLength = pCred->userAgentNameLength;

        pCtx->credential.pAccessKeyId = pCred->pAccessKeyId;
        pCtx->credential.accessKeyIdLength = pCred->accessKeyIdLength;
        pCtx->credential.pSecretAccessKey = pCred->pSecretAccessKey;
        pCtx->credential.secretAccessKeyLength = pCred->secretAccessKeyLength;

        pCtx->credential.pCaCertPath = pCred->pCaCertPath;

        pCtx->credential.pCaCertPem = pCred->pCaCertPem;
        pCtx->credential.caCertPemSize = pCred->caCertPemSize;

        pCtx->receiveMessageCallback = receiveMessageCallback;
        pCtx->pReceiveMessageCallbackContext = pReceiveMessageCallbackContext;
    }

    /* Initialize HTTP. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = SignalingController_HttpInit( pCtx );
    }

    /* Initializa Websocket. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = SignalingController_WebsocketInit( pCtx );
    }

    /* Initializa Message Queue. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Delete message queue from previous round. */
        MessageQueue_Destroy( NULL, SIGNALING_CONTROLLER_MESSAGE_QUEUE_NAME );

        retMessageQueue = MessageQueue_Create( &pCtx->sendMessageQueue, SIGNALING_CONTROLLER_MESSAGE_QUEUE_NAME, sizeof( SignalingControllerEventMessage_t ), MAX_QUEUE_MSG_NUM );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_MQ_INIT_FAIL;
        }
    }

    return ret;
}

void SignalingController_Deinit( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;

    if( pCtx == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Free mqueue. */
        MessageQueue_Destroy( &pCtx->sendMessageQueue, SIGNALING_CONTROLLER_MESSAGE_QUEUE_NAME );
    }
}

SignalingControllerResult_t SignalingController_ConnectServers( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingControllerIceServerConfig_t * pIceServerConfigs;
    size_t iceServerConfigsCount;

    /* Check input parameters. */
    if( pCtx == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    /* Get security token. */
    // if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    // {
    //     ret = getIso8601CurrentTime( &pCtx->credential );
    // }

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
    WebsocketResult_t websocketRet;
    MessageQueueResult_t messageQueueRet;
    SignalingControllerEventMessage_t eventMsg;
    size_t eventMsgLength;

    for( ;; )
    {
        websocketRet = Websocket_Recv();

        if( websocketRet != WEBSOCKET_RESULT_OK )
        {
            LogError( ( "Websocket_Recv fail, return 0x%x", websocketRet ) );
            ret = SIGNALING_CONTROLLER_RESULT_WSS_RECV_FAIL;
            break;
        }

        messageQueueRet = MessageQueue_IsEmpty( &pCtx->sendMessageQueue );
        if( messageQueueRet == MESSAGE_QUEUE_RESULT_MQ_HAVE_MESSAGE )
        {
            /* Handle event. */
            eventMsgLength = sizeof( SignalingControllerEventMessage_t );
            messageQueueRet = MessageQueue_Recv( &pCtx->sendMessageQueue, &eventMsg, &eventMsgLength );
            if( messageQueueRet == MESSAGE_QUEUE_RESULT_OK )
            {
                /* Received message, process it. */
                LogDebug( ( "EventMsg: event: %d, pOnCompleteCallbackContext: %p", eventMsg.event, eventMsg.pOnCompleteCallbackContext ) );
                ret = handleEvent( pCtx, &eventMsg );
            }
        }
    }

    return ret;
}

SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t * pCtx,
                                                             SignalingControllerEventMessage_t * pEventMsg )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    MessageQueueResult_t retMessageQueue;

    if( ( pCtx == NULL ) || ( pEventMsg == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retMessageQueue = MessageQueue_Send( &pCtx->sendMessageQueue, pEventMsg, sizeof( SignalingControllerEventMessage_t ) );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_MQ_SEND_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* Wake the running thread up to handle event. */
        ( void ) Websocket_Signal();
    }

    return ret;
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
            LogWarn( ( "Input message is not valid JSON message, result: %d, message(%d): %.*s",
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
        LogWarn( ( "No target content found in event message, result: %d, SDP target type \"%s\", message(%d): %.*s",
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
                LogWarn( ( "Buffer space is not enough to store formal SDP message, buffer size: %u, SDP message(%u): %.*s",
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
                LogError( ( "The output buffer length(%u) is too small to store serialized %u bytes message.",
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
