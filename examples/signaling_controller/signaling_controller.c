#include <string.h>

#include "signaling_controller.h"
#include "libwebsockets.h"
#include "signaling_api.h"

#define MAX_URI_CHAR_LEN ( 10000 )
#define MAX_JSON_PARAMETER_STRING_LEN ( 10 * 1024 )

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
    describeSignalingChannelRequest.channelNameLength = pCtx->channelNameLength;
    describeSignalingChannelRequest.pChannelName = pCtx->pChannelName;

    retSignal = Signaling_constructDescribeSignalingChannelRequest(&pCtx->signalingContext, &describeSignalingChannelRequest, &signalRequest);
    if( retSignal != SIGNALING_RESULT_OK )
    {
        ret = SIGNALING_CONTROLLER_RESULT_SIGNALING_DESCRIBE_SIGNALING_CHANNEL_FAIL;
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
        response.bufferLen = MAX_JSON_PARAMETER_STRING_LEN;

        ret = Https_Send( &pCtx->httpsContext, &request, 0, &response );
    }

    return ret;
}

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t * pCtx, SignalingControllerCredential_t * pCred )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t retSignal;
    SignalingAwsControlPlaneInfo_t awsControlPlaneInfo;
    HttpsResult_t retHttps;

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
        pCtx->pRegion = pCred->pRegion;
        pCtx->regionLength = pCred->regionLength;
        
        pCtx->pChannelName = pCred->pChannelName;
        pCtx->channelNameLength = pCred->channelNameLength;

        /* Initialize AKSK. */
        pCtx->credential.pAccessKeyId = pCred->pAccessKeyId;
        pCtx->credential.accessKeyIdLen = pCred->accessKeyIdLength;
        pCtx->credential.pSecretAccessKey = pCred->pSecretAccessKey;
        pCtx->credential.secretAccessKeyLen = pCred->secretAccessKeyLength;
    }

    /* Initialize signaling component. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &awsControlPlaneInfo, 0, sizeof( SignalingAwsControlPlaneInfo_t ) );

        awsControlPlaneInfo.pRegion = pCtx->pRegion;
        awsControlPlaneInfo.regionLength = pCtx->regionLength;
        retSignal = Signaling_Init(&pCtx->signalingContext, &awsControlPlaneInfo);

        if( retSignal != SIGNALING_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_SIGNALING_INIT_FAIL;
        }
    }

    /* Initialize HTTPS. */
    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        retHttps = Https_Init( &pCtx->httpsContext, &pCtx->credential );

        if( retHttps != HTTPS_RESULT_OK )
        {
            ret = SIGNALING_CONTROLLER_RESULT_HTTPS_INIT_FAIL;
        }
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

