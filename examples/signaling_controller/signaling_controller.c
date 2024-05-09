#include <string.h>
#include "logging.h"
#include "signaling_controller.h"
#include "libwebsockets.h"
#include "signaling_api.h"
#include "networkingLibwebsockets.h"
#include "base64.h"

#define SIGNALING_CONTROLLER_MESSAGE_QUEUE_NAME "/WebrtcApplicationSignalingController"

#define MAX_URI_CHAR_LEN ( 10000 )
#define MAX_JSON_PARAMETER_STRING_LEN ( 10 * 1024 )
#define MAX_QUEUE_MSG_NUM ( 10 )

static SignalingControllerResult_t updateIceServerConfigs( SignalingControllerContext_t *pCtx, SignalingIceServerList_t *pIceServerList );

static WebsocketResult_t handleWssMessage( char *pMessage, size_t messageLength, void *pUserContext )
{
    WebsocketResult_t ret = WEBSOCKET_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingWssRecvMessage_t wssRecvMessage;
    SignalingControllerContext_t *pCtx = ( SignalingControllerContext_t * ) pUserContext;
    SignalingControllerReceiveEvent_t receiveEvent;
    Base64Result_t retBase64;
    
    // Parse the response
    retSignal = Signaling_parseWssRecvMessage( pMessage, (size_t) messageLength, &wssRecvMessage );
    if( retSignal != SIGNALING_RESULT_OK )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_UNKNOWN_MESSAGE;
    }

    if( ret == WEBSOCKET_RESULT_OK &&
        wssRecvMessage.iceServerList.iceServerNum > 0U &&
        wssRecvMessage.messageType == SIGNALING_TYPE_MESSAGE_SDP_OFFER )
    {
        ret = updateIceServerConfigs( pCtx, &wssRecvMessage.iceServerList );
    }

    /* Decode base64 payload. */
    if( ret == WEBSOCKET_RESULT_OK )
    {
        pCtx->base64BufferLength = SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH;
        retBase64 = base64Decode( wssRecvMessage.pBase64EncodedPayload, wssRecvMessage.base64EncodedPayloadLength, pCtx->base64Buffer, &pCtx->base64BufferLength );

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

static SignalingControllerResult_t HttpLibwebsockets_Init( SignalingControllerContext_t *pCtx )
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

static SignalingControllerResult_t WebsocketLibwebsockets_Init( SignalingControllerContext_t *pCtx )
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

static SignalingControllerResult_t HttpLibwebsockets_PerformRequest( SignalingControllerContext_t *pCtx, HttpRequest_t *pRequest, size_t timeoutMs, HttpResponse_t *pResponse )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    HttpResult_t retHttp;

    retHttp = Http_Send( pRequest, timeoutMs, pResponse );

    if( retHttp != HTTP_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_HTTP_PERFORM_REQUEST_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t WebsocketLibwebsockets_Connect( WebsocketServerInfo_t *pServerInfo )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    WebsocketResult_t retWebsocket;

    retWebsocket = Websocket_Connect( pServerInfo );

    if( retWebsocket != WEBSOCKET_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_WSS_CONNECT_FAIL;
    }

    return ret;
}

static void printMetrics( SignalingControllerContext_t * pCtx )
{
    uint8_t i, j;
    long long duration_ms;

    /* channel info */
    LogDebug( ( "======================================== Channel Info ========================================" ) );
    LogDebug( ( "Signaling Channel Name: %s", pCtx->channelInfo.signalingChannelName ) );
    LogDebug( ( "Signaling Channel ARN: %s", pCtx->channelInfo.signalingChannelARN ) );
    LogDebug( ( "Signaling Channel TTL (seconds): %u", pCtx->channelInfo.signalingChannelTtlSeconds ) );
    LogDebug( ( "======================================== Endpoints Info ========================================" ) );
    LogDebug( ( "HTTPS Endpoint: %s", pCtx->channelInfo.endpointHttps ) );
    LogDebug( ( "WSS Endpoint: %s", pCtx->channelInfo.endpointWebsocketSecure ) );
    LogDebug( ( "WebRTC Endpoint: %s", pCtx->channelInfo.endpointWebrtc[0]=='\0'? "N/A":pCtx->channelInfo.endpointWebrtc ) );

    /* Ice server list */
    LogDebug( ( "======================================== Ice Server List ========================================" ) );
    LogDebug( ( "Ice Server Count: %u", pCtx->iceServerConfigsCount ) );
    for( i=0 ; i<pCtx->iceServerConfigsCount ; i++ )
    {
        LogDebug( ( "======================================== Ice Server[%u] ========================================", i ) );
        LogDebug( ( "    TTL (secodns): %u", pCtx->iceServerConfigs[i].ttlSeconds ) );
        LogDebug( ( "    User Name: %s", pCtx->iceServerConfigs[i].userName ) );
        LogDebug( ( "    Password: %s", pCtx->iceServerConfigs[i].password ) );
        LogDebug( ( "    URI Count: %u", pCtx->iceServerConfigs[i].uriCount ) );
        for( j=0 ; j<pCtx->iceServerConfigs[i].uriCount ; j++ )
        {
            LogDebug( ( "        URI: %s", pCtx->iceServerConfigs[i].uris[j] ) );
        }
    }

    /* Print each step duration */
    LogDebug( ( "======================================== Duration ========================================" ) );
    duration_ms = (pCtx->metrics.describeSignalingChannelEndTime.tv_sec - pCtx->metrics.describeSignalingChannelStartTime.tv_sec) * 1000LL +
                  (pCtx->metrics.describeSignalingChannelEndTime.tv_usec - pCtx->metrics.describeSignalingChannelStartTime.tv_usec) / 1000LL;
    LogDebug( ( "Duration of Describe Signaling Channel: %lld ms", duration_ms ) );
    duration_ms = (pCtx->metrics.getSignalingEndpointsEndTime.tv_sec - pCtx->metrics.getSignalingEndpointsStartTime.tv_sec) * 1000LL +
                  (pCtx->metrics.getSignalingEndpointsEndTime.tv_usec - pCtx->metrics.getSignalingEndpointsStartTime.tv_usec) / 1000LL;
    LogDebug( ( "Duration of Get Signaling Endpoints: %lld ms", duration_ms ) );
    duration_ms = (pCtx->metrics.getIceServerListEndTime.tv_sec - pCtx->metrics.getIceServerListStartTime.tv_sec) * 1000LL +
                  (pCtx->metrics.getIceServerListEndTime.tv_usec - pCtx->metrics.getIceServerListStartTime.tv_usec) / 1000LL;
    LogDebug( ( "Duration of Get Ice Server List: %lld ms", duration_ms ) );
    duration_ms = (pCtx->metrics.connectWssServerEndTime.tv_sec - pCtx->metrics.connectWssServerStartTime.tv_sec) * 1000LL +
                  (pCtx->metrics.connectWssServerEndTime.tv_usec - pCtx->metrics.connectWssServerStartTime.tv_usec) / 1000LL;
    LogDebug( ( "Duration of Connect Websocket Server: %lld ms", duration_ms ) );
}

static SignalingControllerResult_t updateIceServerConfigs( SignalingControllerContext_t *pCtx, SignalingIceServerList_t *pIceServerList )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    uint8_t i, j;

    if( pCtx == NULL || pIceServerList == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        for( i=0 ; i<pIceServerList->iceServerNum ; i++ )
        {
            if( i >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT )
            {
                break;
            }
            else if( pIceServerList->iceServer[i].userNameLength >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_USER_NAME_LENGTH )
            {
                ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_USERNAME;
                break;
            }
            else if( pIceServerList->iceServer[i].passwordLength >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_PASSWORD_LENGTH )
            {
                ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_PASSWORD;
                break;
            }
            else
            {
                /* Do nothing, coverity happy. */
            }

            memcpy( pCtx->iceServerConfigs[i].userName, pIceServerList->iceServer[i].pUserName, pIceServerList->iceServer[i].userNameLength );
            pCtx->iceServerConfigs[i].userNameLength = pIceServerList->iceServer[i].userNameLength;
            memcpy( pCtx->iceServerConfigs[i].password, pIceServerList->iceServer[i].pPassword, pIceServerList->iceServer[i].passwordLength );
            pCtx->iceServerConfigs[i].passwordLength = pIceServerList->iceServer[i].passwordLength;
            pCtx->iceServerConfigs[i].ttlSeconds = pIceServerList->iceServer[i].messageTtlSeconds;

            for( j=0 ; j<pIceServerList->iceServer[i].urisNum ; j++ )
            {
                if( j >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT )
                {
                    break;
                }
                else if( pIceServerList->iceServer[i].urisLength[j] >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_URI_LENGTH )
                {
                    ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_URI;
                    break;
                }
                else
                {
                    /* Do nothing, coverity happy. */
                }

                memcpy( &pCtx->iceServerConfigs[i].uris[j], pIceServerList->iceServer[i].pUris[j], pIceServerList->iceServer[i].urisLength[j] );
                pCtx->iceServerConfigs[i].urisLength[j] = pIceServerList->iceServer[i].urisLength[j];
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
        pCtx->iceServerConfigsCount = i;
    }

    return ret;
}

static SignalingControllerResult_t describeSignalingChannel( SignalingControllerContext_t *pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingRequest_t signalRequest;
    SignalingDescribeSignalingChannelRequest_t describeSignalingChannelRequest;
    SignalingDescribeSignalingChannelResponse_t describeSignalingChannelResponse;
    char url[MAX_URI_CHAR_LEN];
    char paramsJson[MAX_JSON_PARAMETER_STRING_LEN];
    HttpRequest_t request;
    HttpResponse_t response;
    char responseBuffer[MAX_JSON_PARAMETER_STRING_LEN];

    // Prepare URL buffer
    signalRequest.pUrl = &url[0];
    signalRequest.urlLength = MAX_URI_CHAR_LEN;
    // Prepare body buffer
    signalRequest.pBody = &paramsJson[0];
    signalRequest.bodyLength = MAX_JSON_PARAMETER_STRING_LEN;
    // Create the API url
    describeSignalingChannelRequest.pChannelName = pCtx->credential.pChannelName;
    describeSignalingChannelRequest.channelNameLength = pCtx->credential.channelNameLength;

    retSignal = Signaling_constructDescribeSignalingChannelRequest(&pCtx->signalingContext, &describeSignalingChannelRequest, &signalRequest);

    if( retSignal != SIGNALING_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_DESCRIBE_SIGNALING_CHANNEL_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &request, 0, sizeof(HttpRequest_t) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;

        memset( &response, 0, sizeof(HttpResponse_t) );
        response.pBuffer = responseBuffer;
        response.bufferLength = MAX_JSON_PARAMETER_STRING_LEN;

        ret = HttpLibwebsockets_PerformRequest( pCtx, &request, 0, &response );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retSignal = Signaling_parseDescribeSignalingChannelResponse(&pCtx->signalingContext, responseBuffer, response.bufferLength, &describeSignalingChannelResponse);

        if( retSignal != SIGNALING_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_DESCRIBE_SIGNALING_CHANNEL_FAIL;
        }
    }
    
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( describeSignalingChannelResponse.pChannelStatus == NULL || strncmp( describeSignalingChannelResponse.pChannelStatus, "ACTIVE", describeSignalingChannelResponse.channelStatusLength ) != 0 )
        {
            ret = SIGNALING_CONTROLLER_RESULT_INACTIVE_SIGNALING_CHANNEL;
        }
    }

    // Parse the response
    if( ret == SIGNALING_CONTROLLER_RESULT_OK && describeSignalingChannelResponse.pChannelArn != NULL )
    {
        if( describeSignalingChannelResponse.channelArnLength > SIGNALING_AWS_MAX_ARN_LEN )
        {
            /* Return ARN is longer than expectation. Drop it. */
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_ARN;
        }
        else
        {
            strncpy( pCtx->channelInfo.signalingChannelARN, describeSignalingChannelResponse.pChannelArn, describeSignalingChannelResponse.channelArnLength );
            pCtx->channelInfo.signalingChannelARN[describeSignalingChannelResponse.channelArnLength] = '\0';
            pCtx->channelInfo.signalingChannelARNLength = describeSignalingChannelResponse.channelArnLength;
        }
    }
    
    if( ret == SIGNALING_CONTROLLER_RESULT_OK && describeSignalingChannelResponse.pChannelName != NULL )
    {
        if( describeSignalingChannelResponse.channelNameLength > SIGNALING_AWS_MAX_CHANNEL_NAME_LEN )
        {
            /* Return channel name is longer than expectation. Drop it. */
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_SIGNALING_CHANNEL_NAME;
        }
        else
        {
            strncpy( pCtx->channelInfo.signalingChannelName, describeSignalingChannelResponse.pChannelName, describeSignalingChannelResponse.channelNameLength );
            pCtx->channelInfo.signalingChannelName[describeSignalingChannelResponse.channelNameLength] = '\0';
            pCtx->channelInfo.signalingChannelNameLength = describeSignalingChannelResponse.channelNameLength;
        }
    }
    
    if( ret == SIGNALING_CONTROLLER_RESULT_OK && describeSignalingChannelResponse.messageTtlSeconds != 0U )
    {
        pCtx->channelInfo.signalingChannelTtlSeconds = describeSignalingChannelResponse.messageTtlSeconds;
    }

    return ret;
}

static SignalingControllerResult_t getSignalingChannelEndpoints( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingRequest_t signalRequest;
    SignalingGetSignalingChannelEndpointRequest_t getSignalingChannelEndpointRequest;
    SignalingGetSignalingChannelEndpointResponse_t getSignalingChannelEndpointResponse;
    char url[MAX_URI_CHAR_LEN];
    char paramsJson[MAX_JSON_PARAMETER_STRING_LEN];
    HttpRequest_t request;
    HttpResponse_t response;
    char responseBuffer[MAX_JSON_PARAMETER_STRING_LEN];

    // Prepare URL buffer
    signalRequest.pUrl = &url[0];
    signalRequest.urlLength = MAX_URI_CHAR_LEN;
    // Prepare body buffer
    signalRequest.pBody = &paramsJson[0];
    signalRequest.bodyLength = MAX_JSON_PARAMETER_STRING_LEN;
    // Create the API url
    getSignalingChannelEndpointRequest.pChannelArn = pCtx->channelInfo.signalingChannelARN;
    getSignalingChannelEndpointRequest.channelArnLength = pCtx->channelInfo.signalingChannelARNLength;
    getSignalingChannelEndpointRequest.protocolsBitsMap = SIGNALING_ENDPOINT_PROTOCOL_HTTPS | SIGNALING_ENDPOINT_PROTOCOL_WEBSOCKET_SECURE;
    getSignalingChannelEndpointRequest.role = SIGNALING_ROLE_MASTER;

    retSignal = Signaling_constructGetSignalingChannelEndpointRequest(&pCtx->signalingContext, &getSignalingChannelEndpointRequest, &signalRequest);

    if( retSignal != SIGNALING_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &request, 0, sizeof(HttpRequest_t) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;

        memset( &response, 0, sizeof(HttpResponse_t) );
        response.pBuffer = responseBuffer;
        response.bufferLength = MAX_JSON_PARAMETER_STRING_LEN;

        ret = HttpLibwebsockets_PerformRequest( pCtx, &request, 0, &response );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retSignal = Signaling_parseGetSignalingChannelEndpointResponse(&pCtx->signalingContext, responseBuffer, response.bufferLength, &getSignalingChannelEndpointResponse);

        if( retSignal != SIGNALING_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL;
        }
    }
    
    // Parse the response
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( getSignalingChannelEndpointResponse.pEndpointHttps == NULL || getSignalingChannelEndpointResponse.endpointHttpsLength > SIGNALING_AWS_MAX_ARN_LEN )
        {
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_HTTP_ENDPOINT;
        }
        else
        {
            strncpy( pCtx->channelInfo.endpointHttps, getSignalingChannelEndpointResponse.pEndpointHttps, getSignalingChannelEndpointResponse.endpointHttpsLength );
            pCtx->channelInfo.endpointHttps[getSignalingChannelEndpointResponse.endpointHttpsLength] = '\0';
            pCtx->channelInfo.endpointHttpsLength = getSignalingChannelEndpointResponse.endpointHttpsLength;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( getSignalingChannelEndpointResponse.pEndpointWebsocketSecure == NULL || getSignalingChannelEndpointResponse.endpointWebsocketSecureLength > SIGNALING_AWS_MAX_ARN_LEN )
        {
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_WEBSOCKET_SECURE_ENDPOINT;
        }
        else
        {
            strncpy( pCtx->channelInfo.endpointWebsocketSecure, getSignalingChannelEndpointResponse.pEndpointWebsocketSecure, getSignalingChannelEndpointResponse.endpointWebsocketSecureLength );
            pCtx->channelInfo.endpointWebsocketSecure[getSignalingChannelEndpointResponse.endpointWebsocketSecureLength] = '\0';
            pCtx->channelInfo.endpointWebsocketSecureLength = getSignalingChannelEndpointResponse.endpointWebsocketSecureLength;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK && getSignalingChannelEndpointResponse.pEndpointWebrtc != NULL )
    {
        if( getSignalingChannelEndpointResponse.endpointWebrtcLength > SIGNALING_AWS_MAX_ARN_LEN )
        {
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_WEBRTC_ENDPOINT;
        }
        else
        {
            strncpy( pCtx->channelInfo.endpointWebrtc, getSignalingChannelEndpointResponse.pEndpointWebrtc, getSignalingChannelEndpointResponse.endpointWebrtcLength );
            pCtx->channelInfo.endpointWebrtc[getSignalingChannelEndpointResponse.endpointWebrtcLength] = '\0';
            pCtx->channelInfo.endpointWebrtcLength = getSignalingChannelEndpointResponse.endpointWebrtcLength;
        }
    }

    return ret;
}

static SignalingControllerResult_t getIceServerList( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingRequest_t signalRequest;
    SignalingGetIceServerConfigRequest_t getIceServerConfigRequest;
    SignalingGetIceServerConfigResponse_t getIceServerConfigResponse;
    char url[MAX_URI_CHAR_LEN];
    char paramsJson[MAX_JSON_PARAMETER_STRING_LEN];
    HttpRequest_t request;
    HttpResponse_t response;
    char responseBuffer[MAX_JSON_PARAMETER_STRING_LEN];

    // Prepare URL buffer
    signalRequest.pUrl = &url[0];
    signalRequest.urlLength = MAX_URI_CHAR_LEN;
    // Prepare body buffer
    signalRequest.pBody = &paramsJson[0];
    signalRequest.bodyLength = MAX_JSON_PARAMETER_STRING_LEN;
    // Create the API url
    getIceServerConfigRequest.pChannelArn = pCtx->channelInfo.signalingChannelARN;
    getIceServerConfigRequest.channelArnLength = pCtx->channelInfo.signalingChannelARNLength;
    getIceServerConfigRequest.pEndpointHttps = pCtx->channelInfo.endpointHttps;
    getIceServerConfigRequest.endpointHttpsLength = pCtx->channelInfo.endpointHttpsLength;
    getIceServerConfigRequest.pClientId = "ProducerMaster";
    getIceServerConfigRequest.clientIdLength = strlen("ProducerMaster");

    retSignal = Signaling_constructGetIceServerConfigRequest(&pCtx->signalingContext, &getIceServerConfigRequest, &signalRequest);

    if( retSignal != SIGNALING_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_SERVER_LIST_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &request, 0, sizeof(HttpRequest_t) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;

        memset( &response, 0, sizeof(HttpResponse_t) );
        response.pBuffer = responseBuffer;
        response.bufferLength = MAX_JSON_PARAMETER_STRING_LEN;

        ret = HttpLibwebsockets_PerformRequest( pCtx, &request, 0, &response );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retSignal = Signaling_parseGetIceServerConfigResponse(&pCtx->signalingContext, responseBuffer, response.bufferLength, &getIceServerConfigResponse);

        if( retSignal != SIGNALING_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_PARSE_GET_SIGNALING_SERVER_LIST_FAIL;
        }
    }
    
    // Parse the response
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = updateIceServerConfigs( pCtx, &getIceServerConfigResponse );
    }

    return ret;
}

static SignalingControllerResult_t connectWssEndpoint( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingRequest_t signalRequest;
    SignalingConnectWssEndpointRequest_t connectWssEndpointRequest;
    char url[MAX_URI_CHAR_LEN];
    WebsocketServerInfo_t serverInfo;

    // Prepare URL buffer
    signalRequest.pUrl = &url[0];
    signalRequest.urlLength = MAX_URI_CHAR_LEN;
    // Prepare body buffer
    signalRequest.pBody = NULL;
    signalRequest.bodyLength = 0;
    // Create the API url
    memset( &connectWssEndpointRequest, 0, sizeof(SignalingConnectWssEndpointRequest_t) );
    connectWssEndpointRequest.pChannelArn = pCtx->channelInfo.signalingChannelARN;
    connectWssEndpointRequest.channelArnLength = pCtx->channelInfo.signalingChannelARNLength;
    connectWssEndpointRequest.pEndpointWebsocketSecure = pCtx->channelInfo.endpointWebsocketSecure;
    connectWssEndpointRequest.endpointWebsocketSecureLength = pCtx->channelInfo.endpointWebsocketSecureLength;
    connectWssEndpointRequest.role = SIGNALING_ROLE_MASTER;
    // if(connectWssEndpointRequest.role == SIGNALING_ROLE_VIEWER)
    // {
    //     connectWssEndpointRequest.pClientId = pCtx->channelInfo.;
    //     connectWssEndpointRequest.clientIdLength = strlen(pSignalingClient->clientInfo.signalingClientInfo.clientId);
    // }
    retSignal = Signaling_constructConnectWssEndpointRequest(&pCtx->signalingContext, &connectWssEndpointRequest, &signalRequest);

    if( retSignal != SIGNALING_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_GET_SIGNALING_CHANNEL_ENDPOINTS_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        serverInfo.pUrl = signalRequest.pUrl;
        serverInfo.urlLength = signalRequest.urlLength;
        serverInfo.port = 443;
        ret = WebsocketLibwebsockets_Connect( &serverInfo );
    }

    return ret;
}

static SignalingControllerResult_t handleEvent( SignalingControllerContext_t *pCtx, SignalingControllerEventMessage_t *pEventMsg )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    WebsocketResult_t websocketRet;
    size_t ringBufferIndex;
    SignalingControllerEventStatus_t callbackEventStatus = SIGNALING_CONTROLLER_EVENT_STATUS_NONE;
    Base64Result_t retBase64;
    SignalingWssSendMessage_t wssSendMessage;
    SignalingControllerEventContentSend_t *pEventContentSend;
    SignalingResult_t retSignal;

    switch( pEventMsg->event )
    {
        case SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE:
            /* Allocate the ring buffer to store constructed signaling messages. */
            pEventContentSend = &pEventMsg->eventContent;
            callbackEventStatus = SIGNALING_CONTROLLER_EVENT_STATUS_SENT_FAIL;

            /* Then fill the event information, like correlation ID, recipient client ID and base64 encoded message.
             * Note that the message now is not based encoded yet. */
            pCtx->base64BufferLength = SIGNALING_CONTROLLER_MAX_CONTENT_LENGTH;
            retBase64 = base64Encode( pEventContentSend->pDecodeMessage, pEventContentSend->decodeMessageLength, pCtx->base64Buffer, &pCtx->base64BufferLength );
            if( retBase64 != BASE64_RESULT_OK )
            {
                ret = SIGNALING_CONTROLLER_RESULT_BASE64_ENCODE_FAIL;
            }

            if( ret == SIGNALING_CONTROLLER_RESULT_OK )
            {
                /* Construct signaling message into ring buffer. */
                memset( &wssSendMessage, 0, sizeof( SignalingWssSendMessage_t ) );

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
                retSignal = Signaling_constructWssMessage( &wssSendMessage, pCtx->constructedSignalingBuffer, &pCtx->constructedSignalingBufferLength );
                if( retSignal != SIGNALING_RESULT_OK )
                {
                    LogError( ( "Fail to construct Wss message, result: %d", retSignal ) );
                    ret = SIGNALING_CONTROLLER_RESULT_CONSTRUCT_SIGNALING_MSG_FAIL;
                }
            }

            if( ret == SIGNALING_CONTROLLER_RESULT_OK )
            {
                LogDebug( ( "Constructed WSS message length: %ld, message: \n%.*s", pCtx->constructedSignalingBufferLength,
                            ( int ) pCtx->constructedSignalingBufferLength, pCtx->constructedSignalingBuffer ) );

                /* Finally, sent it to websocket layer. */
                websocketRet = Websocket_Send( pCtx->constructedSignalingBuffer, pCtx->constructedSignalingBufferLength );
                if( websocketRet != SIGNALING_RESULT_OK )
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
    
    if( pEventMsg->onCompleteCallback != NULL && callbackEventStatus != SIGNALING_CONTROLLER_EVENT_STATUS_NONE )
    {
        pEventMsg->onCompleteCallback( callbackEventStatus, pEventMsg->pOnCompleteCallbackContext );
    }

    return ret;
}

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t * pCtx, SignalingControllerCredential_t * pCred, SignalingControllerReceiveMessageCallback receiveMessageCallback, void *pReceiveMessageCallbackContext )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingAwsControlPlaneInfo_t awsControlPlaneInfo;
    MessageQueueResult_t retMessageQueue;

    if( pCtx == NULL || pCred == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }
    else if( pCred->pAccessKeyId == NULL || pCred->pSecretAccessKey == NULL )
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

        pCtx->receiveMessageCallback = receiveMessageCallback;
        pCtx->pReceiveMessageCallbackContext = pReceiveMessageCallbackContext;
    }

    /* Initialize signaling component. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &awsControlPlaneInfo, 0, sizeof( SignalingAwsControlPlaneInfo_t ) );

        awsControlPlaneInfo.pRegion = pCtx->credential.pRegion;
        awsControlPlaneInfo.regionLength = pCtx->credential.regionLength;
        retSignal = Signaling_Init(&pCtx->signalingContext, &awsControlPlaneInfo);

        if( retSignal != SIGNALING_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_SIGNALING_INIT_FAIL;
        }
    }

    /* Initialize HTTP. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = HttpLibwebsockets_Init( pCtx );
    }

    /* Initializa Websocket. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = WebsocketLibwebsockets_Init( pCtx );
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

void SignalingController_Deinit( SignalingControllerContext_t *pCtx )
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
        gettimeofday( &pCtx->metrics.describeSignalingChannelStartTime, NULL );
        ret = describeSignalingChannel( pCtx );
        gettimeofday( &pCtx->metrics.describeSignalingChannelEndTime, NULL );
    }

    /* Query signaling channel endpoints with channel ARN. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        gettimeofday( &pCtx->metrics.getSignalingEndpointsStartTime, NULL );
        ret = getSignalingChannelEndpoints( pCtx );
        gettimeofday( &pCtx->metrics.getSignalingEndpointsEndTime, NULL );
    }

    /* Query ICE server list with HTTPS endpoint. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        gettimeofday( &pCtx->metrics.getIceServerListStartTime, NULL );
        ret = getIceServerList( pCtx );
        gettimeofday( &pCtx->metrics.getIceServerListEndTime, NULL );
    }

    /* Connect websocket secure endpoint. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        gettimeofday( &pCtx->metrics.connectWssServerStartTime, NULL );
        ret = connectWssEndpoint( pCtx );
        gettimeofday( &pCtx->metrics.connectWssServerEndTime, NULL );
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

SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t *pCtx, SignalingControllerEventMessage_t *pEventMsg )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    MessageQueueResult_t retMessageQueue;
    WebsocketResult_t websocketRet;

    if( pCtx == NULL || pEventMsg == NULL )
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
        /* Wake the thread running lws_service() up to handle event. */
        websocketRet = Websocket_Signal();
    }

    return ret;
}

SignalingControllerResult_t SignalingController_QueryIceServerConfigs( SignalingControllerContext_t *pCtx, SignalingControllerIceServerConfig_t **ppIceServerConfigs, size_t *pIceServerConfigsCount )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;

    if( pCtx == NULL || ppIceServerConfigs == NULL || pIceServerConfigsCount == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* TODO: check if ICE server configs expire. */
        *ppIceServerConfigs = pCtx->iceServerConfigs;
        *pIceServerConfigsCount = pCtx->iceServerConfigsCount;
    }

    return ret;
}
