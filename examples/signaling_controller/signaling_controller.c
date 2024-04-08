#include <string.h>

#include "signaling_controller.h"
#include "libwebsockets.h"
#include "signaling_api.h"
#include "httpsLibwebsockets.h"

#define MAX_URI_CHAR_LEN ( 10000 )
#define MAX_JSON_PARAMETER_STRING_LEN ( 10 * 1024 )

static SignalingControllerResult_t HttpsLibwebsockets_Init( SignalingControllerContext_t *pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    HttpsResult_t retHttps;
    HttpsLibwebsocketsCredentials_t httpsLibwebsocketsCred;

    httpsLibwebsocketsCred.pUserAgent = pCtx->credential.pUserAgentName;
    httpsLibwebsocketsCred.userAgentLength = pCtx->credential.userAgentNameLength;
    httpsLibwebsocketsCred.pRegion = pCtx->credential.pRegion;
    httpsLibwebsocketsCred.regionLength = pCtx->credential.regionLength;
    httpsLibwebsocketsCred.pAccessKeyId = pCtx->credential.pAccessKeyId;
    httpsLibwebsocketsCred.accessKeyIdLength = pCtx->credential.accessKeyIdLength;
    httpsLibwebsocketsCred.pSecretAccessKey = pCtx->credential.pSecretAccessKey;
    httpsLibwebsocketsCred.secretAccessKeyLength = pCtx->credential.secretAccessKeyLength;
    httpsLibwebsocketsCred.pCaCertPath = pCtx->credential.pCaCertPath;

    retHttps = Https_Init( &pCtx->httpsContext, &httpsLibwebsocketsCred );

    if( retHttps != HTTPS_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_HTTPS_INIT_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t HttpsLibwebsockets_PerformRequest( SignalingControllerContext_t *pCtx, HttpsRequest_t *pRequest, size_t timeoutMs, HttpsResponse_t *pResponse )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    HttpsResult_t retHttps;

    retHttps = Https_Send( &pCtx->httpsContext, pRequest, timeoutMs, pResponse );

    if( retHttps != HTTPS_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_HTTPS_PERFORM_REQUEST_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t updateIceServerConfigs( SignalingControllerContext_t *pCtx, SignalingGetIceServerConfigResponse_t *pGetIceServerConfigResponse )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    uint8_t i, j;

    if( pCtx == NULL || pGetIceServerConfigResponse == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        for( i=0 ; i<pGetIceServerConfigResponse->iceServerNum ; i++ )
        {
            if( i >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_ICE_CONFIG_COUNT )
            {
                break;
            }
            else if( pGetIceServerConfigResponse->iceServer[i].userNameLength >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_USER_NAME_LENGTH )
            {
                ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_USERNAME;
                break;
            }
            else if( pGetIceServerConfigResponse->iceServer[i].passwordLength >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_PASSWORD_LENGTH )
            {
                ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_PASSWORD;
                break;
            }
            else
            {
                /* Do nothing, coverity happy. */
            }

            memcpy( pCtx->iceServerConfigs[i].userName, pGetIceServerConfigResponse->iceServer[i].pUserName, pGetIceServerConfigResponse->iceServer[i].userNameLength );
            pCtx->iceServerConfigs[i].userNameLength = pGetIceServerConfigResponse->iceServer[i].userNameLength;
            memcpy( pCtx->iceServerConfigs[i].password, pGetIceServerConfigResponse->iceServer[i].pPassword, pGetIceServerConfigResponse->iceServer[i].passwordLength );
            pCtx->iceServerConfigs[i].passwordLength = pGetIceServerConfigResponse->iceServer[i].passwordLength;
            pCtx->iceServerConfigs[i].ttlSeconds = pGetIceServerConfigResponse->iceServer[i].messageTtlSeconds;

            for( j=0 ; j<pGetIceServerConfigResponse->iceServer[i].urisNum ; j++ )
            {
                if( j >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT )
                {
                    break;
                }
                else if( pGetIceServerConfigResponse->iceServer[i].urisLength[j] >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_URI_LENGTH )
                {
                    ret = SIGNALING_CONTROLLER_RESULT_INVALID_ICE_SERVER_URI;
                    break;
                }
                else
                {
                    /* Do nothing, coverity happy. */
                }

                memcpy( &pCtx->iceServerConfigs[i].uris[j], pGetIceServerConfigResponse->iceServer[i].pUris[j], pGetIceServerConfigResponse->iceServer[i].urisLength[j] );
                pCtx->iceServerConfigs[i].urisLength[j] = pGetIceServerConfigResponse->iceServer[i].urisLength[j];
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
    HttpsRequest_t request;
    HttpsResponse_t response;
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
        memset( &request, 0, sizeof(HttpsRequest_t) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;

        memset( &response, 0, sizeof(HttpsResponse_t) );
        response.pBuffer = responseBuffer;
        response.bufferLength = MAX_JSON_PARAMETER_STRING_LEN;

        ret = HttpsLibwebsockets_PerformRequest( pCtx, &request, 0, &response );
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
    HttpsRequest_t request;
    HttpsResponse_t response;
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
        memset( &request, 0, sizeof(HttpsRequest_t) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;

        memset( &response, 0, sizeof(HttpsResponse_t) );
        response.pBuffer = responseBuffer;
        response.bufferLength = MAX_JSON_PARAMETER_STRING_LEN;

        ret = HttpsLibwebsockets_PerformRequest( pCtx, &request, 0, &response );
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
            ret = SIGNALING_CONTROLLER_RESULT_INVALID_HTTPS_ENDPOINT;
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

static SignalingControllerResult_t getSignalingServerList( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingRequest_t signalRequest;
    SignalingGetIceServerConfigRequest_t getIceServerConfigRequest;
    SignalingGetIceServerConfigResponse_t getIceServerConfigResponse;
    char url[MAX_URI_CHAR_LEN];
    char paramsJson[MAX_JSON_PARAMETER_STRING_LEN];
    HttpsRequest_t request;
    HttpsResponse_t response;
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
        memset( &request, 0, sizeof(HttpsRequest_t) );
        request.pUrl = signalRequest.pUrl;
        request.urlLength = signalRequest.urlLength;
        request.pBody = signalRequest.pBody;
        request.bodyLength = signalRequest.bodyLength;

        memset( &response, 0, sizeof(HttpsResponse_t) );
        response.pBuffer = responseBuffer;
        response.bufferLength = MAX_JSON_PARAMETER_STRING_LEN;

        ret = HttpsLibwebsockets_PerformRequest( pCtx, &request, 0, &response );
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

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t * pCtx, SignalingControllerCredential_t * pCred )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingAwsControlPlaneInfo_t awsControlPlaneInfo;

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

    /* Initialize HTTPS. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = HttpsLibwebsockets_Init( pCtx );
    }

    return ret;
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
        ret = describeSignalingChannel( pCtx );
    }

    /* Query signaling channel endpoints with channel ARN. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = getSignalingChannelEndpoints( pCtx );
    }

    /* Connect websocket secure endpoint. */

    /* Query ICE server list with HTTPS endpoint. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = getSignalingServerList( pCtx );
    }

    return ret;
}

SignalingControllerResult_t SignalingController_ProcessLoop( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;

    return ret;
}

SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t * pCtx, char * pMessage, size_t messageLength )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;

    return ret;
}

