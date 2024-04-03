#include <string.h>

#include "signaling_controller.h"
#include "libwebsockets.h"
#include "signaling_api.h"
#include "httpsLibwebsockets.h"

#define MAX_URI_CHAR_LEN ( 10000 )
#define MAX_JSON_PARAMETER_STRING_LEN ( 10 * 1024 )

static SignalingControllerResult_t HttpsLibwebsockets_Init( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    HttpsResult_t retHttps;
    HttpsLibwebsocketsCredentials_t httpsLibwebsocketsCred;

    httpsLibwebsocketsCred.pUserAgent = pCtx->signalingControllerCredential.pUserAgentName;
    httpsLibwebsocketsCred.userAgentLength = pCtx->signalingControllerCredential.userAgentNameLength;
    httpsLibwebsocketsCred.pRegion = pCtx->signalingControllerCredential.pRegion;
    httpsLibwebsocketsCred.regionLength = pCtx->signalingControllerCredential.regionLength;
    httpsLibwebsocketsCred.pAccessKeyId = pCtx->signalingControllerCredential.pAccessKeyId;
    httpsLibwebsocketsCred.accessKeyIdLength = pCtx->signalingControllerCredential.accessKeyIdLength;
    httpsLibwebsocketsCred.pSecretAccessKey = pCtx->signalingControllerCredential.pSecretAccessKey;
    httpsLibwebsocketsCred.secretAccessKeyLength = pCtx->signalingControllerCredential.secretAccessKeyLength;
    httpsLibwebsocketsCred.pCaCertPath = pCtx->signalingControllerCredential.pCaCertPath;

    retHttps = Https_Init( &pCtx->httpsContext, &httpsLibwebsocketsCred );

    if( retHttps != HTTPS_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_HTTPS_INIT_FAIL;
    }

    return ret;
}

static SignalingControllerResult_t HttpsLibwebsockets_PerformRequest( SignalingControllerContext_t * pCtx, HttpsRequest_t * pRequest, size_t timeoutMs, HttpsResponse_t *pResponse )
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

static SignalingControllerResult_t describeSignalingChannel( SignalingControllerContext_t * pCtx )
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
    describeSignalingChannelRequest.pChannelName = pCtx->signalingControllerCredential.pChannelName;
    describeSignalingChannelRequest.channelNameLength = pCtx->signalingControllerCredential.channelNameLength;

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
            strncpy( pCtx->signalingChannelInfo.signalingChannelARN, describeSignalingChannelResponse.pChannelArn, describeSignalingChannelResponse.channelArnLength );
            pCtx->signalingChannelInfo.signalingChannelARN[describeSignalingChannelResponse.channelArnLength] = '\0';
        }

        // if (describeSignalingChannelResponse.pChannelName != NULL) {
        //     // CHK(describeSignalingChannelResponse.channelNameLength <= MAX_CHANNEL_NAME_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
        //     STRNCPY(pSignalingClient->channelDescription.channelName, describeSignalingChannelResponse.pChannelName,
        //             describeSignalingChannelResponse.channelNameLength);
        //     pSignalingClient->channelDescription.channelName[describeSignalingChannelResponse.channelNameLength] = '\0';
        // }

        // if( describeSignalingChannelResponse.pChannelStatus == NULL || strncmp( describeSignalingChannelResponse.pChannelStatus, "ACTIVE", describeSignalingChannelResponse.channelStatusLength ) != 0 )
        // {
        //     ret = SIGNALING_CONTROLLER_RESULT_INACTIVE_SIGNALING_CHANNEL;
        // }

        // // if (describeSignalingChannelResponse.pChannelType != NULL) {
        // //     CHK(describeSignalingChannelResponse.channelTypeLength <= MAX_DESCRIBE_CHANNEL_TYPE_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
        // //     pSignalingClient->channelDescription.channelType =
        // //         getChannelTypeFromString((PCHAR) describeSignalingChannelResponse.pChannelType, describeSignalingChannelResponse.channelTypeLength);
        // // }

        // if (describeSignalingChannelResponse.pVersion != NULL) {
        //     CHK(describeSignalingChannelResponse.versionLength <= MAX_UPDATE_VERSION_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
        //     STRNCPY(pSignalingClient->channelDescription.updateVersion, describeSignalingChannelResponse.pVersion,
        //             describeSignalingChannelResponse.versionLength);
        //     pSignalingClient->channelDescription.updateVersion[describeSignalingChannelResponse.versionLength] = '\0';
        // }

        // if (describeSignalingChannelResponse.messageTtlSeconds != 0) {
        //     // NOTE: Ttl value is in seconds
        //     pSignalingClient->channelDescription.messageTtl = describeSignalingChannelResponse.messageTtlSeconds * HUNDREDS_OF_NANOS_IN_A_SECOND;
        // }
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
            strncpy( pCtx->signalingChannelInfo.signalingChannelName, describeSignalingChannelResponse.pChannelName, describeSignalingChannelResponse.channelNameLength );
            pCtx->signalingChannelInfo.signalingChannelName[describeSignalingChannelResponse.channelNameLength] = '\0';
        }
    }
    
    if( ret == SIGNALING_CONTROLLER_RESULT_OK && describeSignalingChannelResponse.messageTtlSeconds != 0U )
    {
        pCtx->signalingChannelInfo.signalingChannelTtlSeconds = describeSignalingChannelResponse.messageTtlSeconds;
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
        pCtx->signalingControllerCredential.pRegion = pCred->pRegion;
        pCtx->signalingControllerCredential.regionLength = pCred->regionLength;
        
        pCtx->signalingControllerCredential.pChannelName = pCred->pChannelName;
        pCtx->signalingControllerCredential.channelNameLength = pCred->channelNameLength;
        
        pCtx->signalingControllerCredential.pUserAgentName = pCred->pUserAgentName;
        pCtx->signalingControllerCredential.userAgentNameLength = pCred->userAgentNameLength;

        pCtx->signalingControllerCredential.pAccessKeyId = pCred->pAccessKeyId;
        pCtx->signalingControllerCredential.accessKeyIdLength = pCred->accessKeyIdLength;
        pCtx->signalingControllerCredential.pSecretAccessKey = pCred->pSecretAccessKey;
        pCtx->signalingControllerCredential.secretAccessKeyLength = pCred->secretAccessKeyLength;

        pCtx->signalingControllerCredential.pCaCertPath = pCred->pCaCertPath;

        /* Initialize channel info. */
        memset( &pCtx->signalingChannelInfo, 0, sizeof( pCtx->signalingChannelInfo ) );
    }

    /* Initialize signaling component. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &awsControlPlaneInfo, 0, sizeof( SignalingAwsControlPlaneInfo_t ) );

        awsControlPlaneInfo.pRegion = pCtx->signalingControllerCredential.pRegion;
        awsControlPlaneInfo.regionLength = pCtx->signalingControllerCredential.regionLength;
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

    /* Execute create channel if no channel exist (not recommend). */

    /* Query signaling channel endpoints with channel ARN. */

    /* Connect websocket secure endpoint. */

    /* Query ICE server list with HTTPS endpoint. */

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

