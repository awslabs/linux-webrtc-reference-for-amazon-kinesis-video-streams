#include "base64.h"
#include "metric.h"
#include "logging.h"
#include "core_json.h"
#include "networking_utils.h"
#include "signaling_controller.h"

/*----------------------------------------------------------------------------*/

#ifndef MIN
    #define MIN( a,b ) ( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#endif

/*----------------------------------------------------------------------------*/

static int OnWssMessageReceived( char * pMessage,
                                 size_t messageLength,
                                 void * pUserData );

static SignalingControllerResult_t HttpSend( SignalingControllerContext_t * pCtx,
                                             HttpRequest_t * pRequest,
                                             HttpResponse_t * pResponse );

static SignalingControllerResult_t FetchTemporaryCredentials( SignalingControllerContext_t * pCtx,
                                                              const AwsIotCredentials_t * pAwsIotCredentials );

static SignalingControllerResult_t DescribeSignalingChannel( SignalingControllerContext_t * pCtx,
                                                             const SignalingChannelName_t * pChannelName );

static SignalingControllerResult_t GetSignalingChannelEndpoints( SignalingControllerContext_t * pCtx );

static SignalingControllerResult_t GetIceServerConfigs( SignalingControllerContext_t * pCtx );

static SignalingControllerResult_t ConnectToWssEndpoint( SignalingControllerContext_t * pCtx );

static SignalingControllerResult_t ConnectToSignalingService( SignalingControllerContext_t * pCtx,
                                                              const SignalingControllerConnectInfo_t * pConnectInfo );

static void LogSignalingInfo( SignalingControllerContext_t * pCtx );

/*----------------------------------------------------------------------------*/

static int OnWssMessageReceived( char * pMessage,
                                 size_t messageLength,
                                 void * pUserData )
{
    int ret = 0;
    SignalingResult_t signalingResult;
    WssRecvMessage_t wssRecvMessage;
    SignalingControllerContext_t * pCtx = ( SignalingControllerContext_t * ) pUserData;
    SignalingMessage_t signalingMessage;
    Base64Result_t base64Result;

    signalingResult = Signaling_ParseWssRecvMessage( pMessage, messageLength, &( wssRecvMessage ) );
    if( signalingResult != SIGNALING_RESULT_OK )
    {
        LogError( ( "Failed to parse the WSS message. Result: %d!", signalingResult ) );
        ret = 1;
    }

    if( ret == 0 )
    {
        pCtx->signalingRxMessageLength = SIGNALING_CONTROLLER_MESSAGE_BUFFER_LENGTH;
        base64Result = Base64_Decode( wssRecvMessage.pBase64EncodedPayload,
                                      wssRecvMessage.base64EncodedPayloadLength,
                                      &( pCtx->signalingRxMessageBuffer[ 0 ] ),
                                      &( pCtx->signalingRxMessageLength ) );

        if( base64Result != BASE64_RESULT_OK )
        {
            LogError( ( "Failed to decode signaling message. Result: %d!", base64Result ) );
            ret = 1;
        }
    }

    if( ret == 0 )
    {
        switch( wssRecvMessage.messageType )
        {
            case SIGNALING_TYPE_MESSAGE_GO_AWAY:
            {
                LogInfo( ( "Received GOAWAY message from server. Closing connection." ) );
                ret = 1;
            }
            break;

            case SIGNALING_TYPE_MESSAGE_STATUS_RESPONSE:
            {
                if( strcmp( wssRecvMessage.statusResponse.pStatusCode,"200" ) != 0 )
                {
                    LogWarn( ( "Failed to deliver message. Correlation ID: %s, Error Type: %s, Error Code: %s, Description: %s!",
                               wssRecvMessage.statusResponse.pCorrelationId,
                               wssRecvMessage.statusResponse.pErrorType,
                               wssRecvMessage.statusResponse.pStatusCode,
                               wssRecvMessage.statusResponse.pDescription ) );
                }
            }
            break;

            default:
                break;
        }
    }

    if( ( ret == 0 ) && ( pCtx->messageReceivedCallback != NULL ) )
    {
        memset( &( signalingMessage ), 0, sizeof( SignalingMessage_t ) );

        signalingMessage.pRemoteClientId = wssRecvMessage.pSenderClientId;
        signalingMessage.remoteClientIdLength = wssRecvMessage.senderClientIdLength;
        signalingMessage.pCorrelationId = wssRecvMessage.statusResponse.pCorrelationId;
        signalingMessage.correlationIdLength = wssRecvMessage.statusResponse.correlationIdLength;
        signalingMessage.messageType = wssRecvMessage.messageType;
        signalingMessage.pMessage = &( pCtx->signalingRxMessageBuffer[ 0 ] );
        signalingMessage.messageLength = pCtx->signalingRxMessageLength;

        pCtx->messageReceivedCallback( &( signalingMessage ),
                                       pCtx->pMessageReceivedCallbackData );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static SignalingControllerResult_t HttpSend( SignalingControllerContext_t * pCtx,
                                             HttpRequest_t * pRequest,
                                             HttpResponse_t * pResponse )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    NetworkingResult_t networkingResult;
    AwsCredentials_t awsCreds;
    int i;

    pRequest->pUserAgent = pCtx->pUserAgentName;
    pRequest->userAgentLength = pCtx->userAgentNameLength;

    awsCreds.pAccessKeyId = &( pCtx->accessKeyId[ 0 ] );
    awsCreds.accessKeyIdLen = pCtx->accessKeyIdLength;

    awsCreds.pSecretAccessKey = &( pCtx->secretAccessKey[ 0 ] );
    awsCreds.secretAccessKeyLen = pCtx->secretAccessKeyLength;

    awsCreds.pSessionToken = &( pCtx->sessionToken[ 0 ] );
    awsCreds.sessionTokenLength = pCtx->sessionTokenLength;
    awsCreds.expirationSeconds = pCtx->expirationSeconds;

    for( i = 0; i < SIGNALING_CONTROLLER_HTTP_NUM_RETRIES; i++ )
    {
       networkingResult = Networking_HttpSend( &( pCtx->httpContext ),
                                               pRequest,
                                               &( awsCreds ),
                                               &( pCtx->awsConfig ),
                                               pResponse );

        if( networkingResult == NETWORKING_RESULT_OK )
        {
            break;
        }
    }

    if( networkingResult != NETWORKING_RESULT_OK )
    {
        LogError( ( "Networking_HttpSend fails with return 0x%x!", networkingResult ) );
        ret = SIGNALING_CONTROLLER_RESULT_FAIL;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static SignalingControllerResult_t FetchTemporaryCredentials( SignalingControllerContext_t * pCtx,
                                                              const AwsIotCredentials_t * pAwsIotCredentials )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t signalingResult;
    SignalingRequest_t signalingRequest;
    SignalingCredential_t signalingCredentials;
    HttpRequest_t httpRequest;
    HttpResponse_t httpResponse;
    HttpRequestHeader_t httpHeader[ 1 ];

    signalingRequest.pUrl = &( pCtx->httpUrlBuffer[ 0 ] );
    signalingRequest.urlLength = SIGNALING_CONTROLLER_HTTP_URL_BUFFER_LENGTH;

    signalingResult = Signaling_ConstructFetchTempCredsRequestForAwsIot( pAwsIotCredentials->pIotCredentialsEndpoint,
                                                                         pAwsIotCredentials->iotCredentialsEndpointLength,
                                                                         pAwsIotCredentials->pRoleAlias,
                                                                         pAwsIotCredentials->roleAliasLength,
                                                                         &( signalingRequest ) );

    if( signalingResult != SIGNALING_RESULT_OK )
    {
        LogError( ( "Failed to construct Fetch Temporary Credential request. Return = 0x%x!", signalingResult ) );
        ret = SIGNALING_CONTROLLER_RESULT_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        httpHeader[ 0 ].pName = "x-amzn-iot-thingname";
        httpHeader[ 0 ].pValue = pAwsIotCredentials->pThingName;
        httpHeader[ 0 ].valueLength = pAwsIotCredentials->thingNameLength;

        memset( &( httpRequest ), 0, sizeof( HttpRequest_t ) );
        httpRequest.pUrl = signalingRequest.pUrl;
        httpRequest.urlLength = signalingRequest.urlLength;
        httpRequest.pHeaders = &( httpHeader[ 0 ] );
        httpRequest.numHeaders = 1;
        httpRequest.pUserAgent = pCtx->pUserAgentName;
        httpRequest.userAgentLength = pCtx->userAgentNameLength;
        httpRequest.verb = HTTP_GET;

        memset( &( httpResponse ), 0, sizeof( HttpResponse_t ) );
        httpResponse.pBuffer = &( pCtx->httpResponserBuffer[ 0 ] );
        httpResponse.bufferLength = SIGNALING_CONTROLLER_HTTP_RESPONSE_BUFFER_LENGTH;

        if( Networking_HttpSend( &( pCtx->httpContext ),
                                 &( httpRequest ),
                                 NULL,
                                 &( pCtx->awsConfig ),
                                 &( httpResponse ) ) != NETWORKING_RESULT_OK )
        {
            LogError( ( "Failed to fetch temporary credentials!" ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;

        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        signalingResult = Signaling_ParseFetchTempCredsResponseFromAwsIot( httpResponse.pBuffer,
                                                                           httpResponse.bufferLength,
                                                                           &( signalingCredentials ) );

        if( signalingResult != SIGNALING_RESULT_OK )
        {
            LogError( ( "Fail to parse fetch credentials response, return=0x%x, response(%lu): %.*s",
                        signalingResult,
                        httpResponse.bufferLength,
                        ( int ) httpResponse.bufferLength, httpResponse.pBuffer ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
        else
        {
            LogDebug( ( "Access Key ID: %.*s \n\n Secret Access Key ID : %.*s \n\n"
                        "Session Token: %.*s \n \n Expiration: %.*s",
                        ( int ) signalingCredentials.accessKeyIdLength, signalingCredentials.pAccessKeyId,
                        ( int ) signalingCredentials.secretAccessKeyLength, signalingCredentials.pSecretAccessKey,
                        ( int ) signalingCredentials.sessionTokenLength, signalingCredentials.pSessionToken,
                        ( int ) signalingCredentials.expirationLength, ( char * ) signalingCredentials.pExpiration ) );
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( signalingCredentials.pAccessKeyId != NULL ) )
    {
        memcpy( &( pCtx->accessKeyId[ 0 ] ),
                signalingCredentials.pAccessKeyId,
                signalingCredentials.accessKeyIdLength );
        pCtx->accessKeyIdLength = signalingCredentials.accessKeyIdLength;
        pCtx->accessKeyId[ signalingCredentials.accessKeyIdLength ] = '\0';
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( signalingCredentials.pSecretAccessKey != NULL ) )
    {
        memcpy( &( pCtx->secretAccessKey[ 0 ] ),
                signalingCredentials.pSecretAccessKey,
                signalingCredentials.secretAccessKeyLength );
        pCtx->secretAccessKeyLength = signalingCredentials.secretAccessKeyLength;
        pCtx->secretAccessKey[ signalingCredentials.secretAccessKeyLength ] = '\0';
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( signalingCredentials.pSessionToken != NULL ) )
    {
        memcpy( &( pCtx->sessionToken[ 0 ] ),
                signalingCredentials.pSessionToken,
                signalingCredentials.sessionTokenLength );
        pCtx->sessionTokenLength = signalingCredentials.sessionTokenLength;
        pCtx->sessionToken[ signalingCredentials.sessionTokenLength ] = '\0';
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( signalingCredentials.pExpiration != NULL ) )
    {
        pCtx->expirationSeconds = NetworkingUtils_GetTimeFromIso8601( signalingCredentials.pExpiration,
                                                                      signalingCredentials.expirationLength );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static SignalingControllerResult_t DescribeSignalingChannel( SignalingControllerContext_t * pCtx,
                                                             const SignalingChannelName_t * pChannelName )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t signalingResult;
    SignalingRequest_t signalingRequest;
    SignalingAwsRegion_t awsRegion;
    SignalingChannelInfo_t signalingChannelInfo;
    HttpRequest_t httpRequest;
    HttpResponse_t httpResponse;

    awsRegion.pAwsRegion = pCtx->awsConfig.pRegion;
    awsRegion.awsRegionLength = pCtx->awsConfig.regionLen;

    signalingRequest.pUrl = &( pCtx->httpUrlBuffer[ 0 ] );
    signalingRequest.urlLength = SIGNALING_CONTROLLER_HTTP_URL_BUFFER_LENGTH;

    signalingRequest.pBody = &( pCtx->httpBodyBuffer[ 0 ] );
    signalingRequest.bodyLength = SIGNALING_CONTROLLER_HTTP_BODY_BUFFER_LENGTH;

    signalingResult = Signaling_ConstructDescribeSignalingChannelRequest( &( awsRegion ),
                                                                          ( SignalingChannelName_t * ) pChannelName,
                                                                          &( signalingRequest ) );

    if( signalingResult != SIGNALING_RESULT_OK )
    {
        LogError( ( "Fail to construct describe signaling channel request, return=0x%x", signalingResult ) );
        ret = SIGNALING_CONTROLLER_RESULT_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &( httpRequest ), 0, sizeof( HttpRequest_t ) );
        httpRequest.pUrl = signalingRequest.pUrl;
        httpRequest.urlLength = signalingRequest.urlLength;
        httpRequest.pBody = signalingRequest.pBody;
        httpRequest.bodyLength = signalingRequest.bodyLength;
        httpRequest.verb = HTTP_POST;

        memset( &( httpResponse ), 0, sizeof( HttpResponse_t ) );
        httpResponse.pBuffer = &( pCtx->httpResponserBuffer[ 0 ] );
        httpResponse.bufferLength = SIGNALING_CONTROLLER_HTTP_RESPONSE_BUFFER_LENGTH;

        ret = HttpSend( pCtx, &( httpRequest ), &( httpResponse ) );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        signalingResult = Signaling_ParseDescribeSignalingChannelResponse( httpResponse.pBuffer,
                                                                           httpResponse.bufferLength,
                                                                           &( signalingChannelInfo ) );

        if( signalingResult != SIGNALING_RESULT_OK )
        {
            LogError( ( "Fail to parse describe signaling channel response, return=0x%x, response(%lu): %.*s",
                      signalingResult,
                      httpResponse.bufferLength,
                      ( int ) httpResponse.bufferLength, httpResponse.pBuffer ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( ( signalingChannelInfo.pChannelStatus == NULL ) ||
            ( strncmp( signalingChannelInfo.pChannelStatus, "ACTIVE", signalingChannelInfo.channelStatusLength ) != 0 ) )
        {
            LogError( ( "No active channel found!" ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( signalingChannelInfo.channelArn.pChannelArn != NULL ) )
    {
        strncpy( &( pCtx->signalingChannelArn[ 0 ] ),
                 signalingChannelInfo.channelArn.pChannelArn,
                 signalingChannelInfo.channelArn.channelArnLength );
        pCtx->signalingChannelArn[ signalingChannelInfo.channelArn.channelArnLength ] = '\0';
        pCtx->signalingChannelArnLength = signalingChannelInfo.channelArn.channelArnLength;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static SignalingControllerResult_t GetSignalingChannelEndpoints( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t signalingResult;
    SignalingRequest_t signalingRequest;
    SignalingAwsRegion_t awsRegion;
    GetSignalingChannelEndpointRequestInfo_t endpointRequestInfo;
    SignalingChannelEndpoints_t signalingEndpoints;
    HttpRequest_t httpRequest;
    HttpResponse_t httpResponse;

    awsRegion.pAwsRegion = pCtx->awsConfig.pRegion;
    awsRegion.awsRegionLength = pCtx->awsConfig.regionLen;

    signalingRequest.pUrl = &( pCtx->httpUrlBuffer[ 0 ] );
    signalingRequest.urlLength = SIGNALING_CONTROLLER_HTTP_URL_BUFFER_LENGTH;

    signalingRequest.pBody = &( pCtx->httpBodyBuffer[ 0 ] );
    signalingRequest.bodyLength = SIGNALING_CONTROLLER_HTTP_BODY_BUFFER_LENGTH;

    endpointRequestInfo.channelArn.pChannelArn = &( pCtx->signalingChannelArn[ 0 ] );
    endpointRequestInfo.channelArn.channelArnLength = pCtx->signalingChannelArnLength;
    endpointRequestInfo.protocols = SIGNALING_PROTOCOL_WEBSOCKET_SECURE | SIGNALING_PROTOCOL_HTTPS;
    endpointRequestInfo.role = SIGNALING_ROLE_MASTER;

    signalingResult = Signaling_ConstructGetSignalingChannelEndpointRequest( &( awsRegion ),
                                                                             &( endpointRequestInfo ),
                                                                             &( signalingRequest ) );

    if( signalingResult != SIGNALING_RESULT_OK )
    {
        LogError( ( "Failed to construct Get Signaling Channel Endpoint request. return=0x%x!", signalingResult ) );
        ret = SIGNALING_CONTROLLER_RESULT_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &( httpRequest ), 0, sizeof( HttpRequest_t ) );
        httpRequest.pUrl = signalingRequest.pUrl;
        httpRequest.urlLength = signalingRequest.urlLength;
        httpRequest.pBody = signalingRequest.pBody;
        httpRequest.bodyLength = signalingRequest.bodyLength;
        httpRequest.verb = HTTP_POST;

        memset( &( httpResponse ), 0, sizeof( HttpResponse_t ) );
        httpResponse.pBuffer = &( pCtx->httpResponserBuffer[ 0 ] );
        httpResponse.bufferLength = SIGNALING_CONTROLLER_HTTP_RESPONSE_BUFFER_LENGTH;

        ret = HttpSend( pCtx, &( httpRequest ), &( httpResponse ) );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        signalingResult = Signaling_ParseGetSignalingChannelEndpointResponse( httpResponse.pBuffer,
                                                                              httpResponse.bufferLength,
                                                                              &( signalingEndpoints ) );

        if( signalingResult != SIGNALING_RESULT_OK )
        {
            LogError( ( "Failed to parse Get Signaling Channel Endpoint response. return=0x%x!", signalingResult ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( signalingEndpoints.httpsEndpoint.pEndpoint != NULL ) )
    {
        if( signalingEndpoints.httpsEndpoint.endpointLength > SIGNALING_CONTROLLER_ENDPOINT_BUFFER_LENGTH )
        {
            LogError( ( "HTTPS endpoint (%lu) does not fit in the buffer!", signalingEndpoints.httpsEndpoint.endpointLength ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
        else
        {
            strncpy( &( pCtx->httpsEndpoint[ 0 ] ),
                     signalingEndpoints.httpsEndpoint.pEndpoint,
                     signalingEndpoints.httpsEndpoint.endpointLength );
            pCtx->httpsEndpoint[ signalingEndpoints.httpsEndpoint.endpointLength ] = '\0';
            pCtx->httpsEndpointLength = signalingEndpoints.httpsEndpoint.endpointLength;
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( signalingEndpoints.wssEndpoint.pEndpoint != NULL ) )
    {
        if( signalingEndpoints.wssEndpoint.endpointLength > SIGNALING_CONTROLLER_ENDPOINT_BUFFER_LENGTH )
        {
            LogError( ( "WSS endpoint (%lu) does not fit in the buffer!", signalingEndpoints.wssEndpoint.endpointLength ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
        else
        {
            strncpy( &( pCtx->wssEndpoint[ 0 ] ),
                     signalingEndpoints.wssEndpoint.pEndpoint,
                     signalingEndpoints.wssEndpoint.endpointLength );
            pCtx->wssEndpoint[ signalingEndpoints.wssEndpoint.endpointLength ] = '\0';
            pCtx->wssEndpointLength = signalingEndpoints.wssEndpoint.endpointLength;
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( signalingEndpoints.webrtcEndpoint.pEndpoint != NULL ) )
    {
        if( signalingEndpoints.webrtcEndpoint.endpointLength > SIGNALING_CONTROLLER_ENDPOINT_BUFFER_LENGTH )
        {
            LogError( ( "WebRTC endpoint (%lu) does not fit in the buffer!", signalingEndpoints.webrtcEndpoint.endpointLength ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
        else
        {
            strncpy( &( pCtx->webrtcEndpoint[ 0 ] ),
                     signalingEndpoints.webrtcEndpoint.pEndpoint,
                     signalingEndpoints.webrtcEndpoint.endpointLength );
            pCtx->webrtcEndpoint[ signalingEndpoints.webrtcEndpoint.endpointLength ] = '\0';
            pCtx->webrtcEndpointLength = signalingEndpoints.webrtcEndpoint.endpointLength;
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static SignalingControllerResult_t GetIceServerConfigs( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t signalingResult;
    SignalingRequest_t signalingRequest;
    SignalingChannelEndpoint_t signalingChannelHttpEndpoint;
    GetIceServerConfigRequestInfo_t getIceServerConfigRequestInfo;
    HttpRequest_t httpRequest;
    HttpResponse_t httpResponse;
    SignalingIceServer_t iceServers[ SIGNALING_CONTROLLER_ICE_SERVER_MAX_CONFIG_COUNT ];
    size_t iceServersCount = SIGNALING_CONTROLLER_ICE_SERVER_MAX_CONFIG_COUNT;
    size_t i, j;
    uint32_t minTtl = UINT32_MAX;
    uint64_t iceServerConfigTimeSec;

    signalingChannelHttpEndpoint.pEndpoint = &( pCtx->httpsEndpoint[ 0 ] );
    signalingChannelHttpEndpoint.endpointLength = pCtx->httpsEndpointLength;

    signalingRequest.pUrl = &( pCtx->httpUrlBuffer[ 0 ] );
    signalingRequest.urlLength = SIGNALING_CONTROLLER_HTTP_URL_BUFFER_LENGTH;

    signalingRequest.pBody = &( pCtx->httpBodyBuffer[ 0 ] );
    signalingRequest.bodyLength = SIGNALING_CONTROLLER_HTTP_BODY_BUFFER_LENGTH;

    getIceServerConfigRequestInfo.channelArn.pChannelArn = &( pCtx->signalingChannelArn[ 0 ] );
    getIceServerConfigRequestInfo.channelArn.channelArnLength = pCtx->signalingChannelArnLength;
    getIceServerConfigRequestInfo.pClientId = "ProducerMaster";
    getIceServerConfigRequestInfo.clientIdLength = strlen( "ProducerMaster" );

    signalingResult = Signaling_ConstructGetIceServerConfigRequest( &( signalingChannelHttpEndpoint ),
                                                                    &( getIceServerConfigRequestInfo ),
                                                                    &( signalingRequest ) );

    if( signalingResult != SIGNALING_RESULT_OK )
    {
        LogError( ( "Failed to construct Get ICE Server Config request. Return=0x%x!", signalingResult ) );
        ret = SIGNALING_CONTROLLER_RESULT_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( &( httpRequest ), 0, sizeof( HttpRequest_t ) );
        httpRequest.pUrl = signalingRequest.pUrl;
        httpRequest.urlLength = signalingRequest.urlLength;
        httpRequest.pBody = signalingRequest.pBody;
        httpRequest.bodyLength = signalingRequest.bodyLength;
        httpRequest.verb = HTTP_POST;

        memset( &( httpResponse ), 0, sizeof( HttpResponse_t ) );
        httpResponse.pBuffer = &( pCtx->httpResponserBuffer[ 0 ] );
        httpResponse.bufferLength = SIGNALING_CONTROLLER_HTTP_RESPONSE_BUFFER_LENGTH;

        ret = HttpSend( pCtx, &( httpRequest ), &( httpResponse ) );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        signalingResult = Signaling_ParseGetIceServerConfigResponse( httpResponse.pBuffer,
                                                                     httpResponse.bufferLength,
                                                                     &( iceServers[ 0 ] ),
                                                                     &( iceServersCount ) );

        if( signalingResult != SIGNALING_RESULT_OK )
        {
            LogError( ( "Failed to parse Get ICE Server Config response. Return=0x%x!", signalingResult ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        for( i = 0; i < iceServersCount; i++ )
        {
            if( i >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_CONFIG_COUNT )
            {
                LogWarn( ( "Cannot store all the ICE server configs! Consider increasing SIGNALING_CONTROLLER_ICE_SERVER_MAX_CONFIG_COUNT. " ) );
                break;
            }

            if( iceServers[ i ].userNameLength >= SIGNALING_CONTROLLER_ICE_SERVER_USER_NAME_BUFFER_LENGTH )
            {
                LogError( ( "ICE server username (%lu) does not fit in the buffer!", iceServers[ i ].userNameLength ) );
                ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                break;
            }

            if( iceServers[ i ].passwordLength >= SIGNALING_CONTROLLER_ICE_SERVER_PASSWORD_BUFFER_LENGTH )
            {
                LogError( ( "ICE server password (%lu) does not fit in the buffer!", iceServers[ i ].passwordLength ) );
                ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                break;
            }

            memcpy( &( pCtx->iceServerConfigs[ i ].userName[ 0 ] ),
                    iceServers[ i ].pUserName,
                    iceServers[ i ].userNameLength );
            pCtx->iceServerConfigs[ i ].userNameLength = iceServers[ i ].userNameLength;

            memcpy( &( pCtx->iceServerConfigs[ i ].password[ 0 ] ),
                    iceServers[ i ].pPassword,
                    iceServers[ i ].passwordLength );
            pCtx->iceServerConfigs[ i ].passwordLength = iceServers[ i ].passwordLength;
            pCtx->iceServerConfigs[ i ].ttlSeconds = iceServers[ i ].messageTtlSeconds;

            minTtl = MIN( minTtl, pCtx->iceServerConfigs[i].ttlSeconds );

            for( j = 0; j < iceServers[ i ].urisNum; j++ )
            {
                if( j >= SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT )
                {
                    LogWarn( ( "Cannot store all the ICE server URIs! Consider increasing SIGNALING_CONTROLLER_ICE_SERVER_MAX_URIS_COUNT. " ) );
                    break;
                }

                if( iceServers[ i ].urisLength[ j ] >= SIGNALING_CONTROLLER_ICE_SERVER_URI_BUFFER_LENGTH )
                {
                    LogError( ( "ICE server URI ( %lu) does not fit in the buffer!", iceServers[ i ].urisLength[ j ] ) );
                    ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                    break;
                }

                memcpy( &( pCtx->iceServerConfigs[ i ].iceServerUris[ j ].uri[ 0 ] ),
                        iceServers[ i ].pUris[ j ],
                        iceServers[ i ].urisLength[ j ] );
                pCtx->iceServerConfigs[ i ].iceServerUris[ j ].uriLength = iceServers[ i ].urisLength[ j ];
            }

            if( ret == SIGNALING_CONTROLLER_RESULT_OK )
            {
                pCtx->iceServerConfigs[ i ].iceServerUriCount = j;
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

        iceServerConfigTimeSec = NetworkingUtils_GetCurrentTimeSec( NULL );

        if( minTtl < SIGNALING_CONTROLLER_ICE_CONFIG_REFRESH_GRACE_PERIOD_SEC )
        {
            LogWarn( ( "Minimum TTL is less than Refresh Grace Period!" ) );
            pCtx->iceServerConfigExpirationSec = iceServerConfigTimeSec + minTtl;
        }
        else
        {
            pCtx->iceServerConfigExpirationSec = iceServerConfigTimeSec +
                                                 ( minTtl - SIGNALING_CONTROLLER_ICE_CONFIG_REFRESH_GRACE_PERIOD_SEC );
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static SignalingControllerResult_t ConnectToWssEndpoint( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    SignalingResult_t signalingResult;
    SignalingRequest_t signalingRequest;
    SignalingChannelEndpoint_t wssEndpoint;
    ConnectWssEndpointRequestInfo_t wssEndpointRequestInfo;
    WebsocketConnectInfo_t wssConnectInfo;
    NetworkingResult_t networkingResult;
    AwsCredentials_t awsCreds;

    wssEndpoint.pEndpoint = &( pCtx->wssEndpoint[ 0 ] );
    wssEndpoint.endpointLength = pCtx->wssEndpointLength;

    signalingRequest.pUrl = &( pCtx->httpUrlBuffer[ 0 ] );
    signalingRequest.urlLength = SIGNALING_CONTROLLER_HTTP_URL_BUFFER_LENGTH;

    signalingRequest.pBody = &( pCtx->httpBodyBuffer[ 0 ] );
    signalingRequest.bodyLength = SIGNALING_CONTROLLER_HTTP_BODY_BUFFER_LENGTH;

    memset( &( wssEndpointRequestInfo ), 0, sizeof( ConnectWssEndpointRequestInfo_t ) );
    wssEndpointRequestInfo.channelArn.pChannelArn = pCtx->signalingChannelArn;
    wssEndpointRequestInfo.channelArn.channelArnLength = pCtx->signalingChannelArnLength;
    wssEndpointRequestInfo.role = SIGNALING_ROLE_MASTER;

    signalingResult = Signaling_ConstructConnectWssEndpointRequest( &( wssEndpoint ),
                                                                    &( wssEndpointRequestInfo ),
                                                                    &( signalingRequest ) );

    if( signalingResult != SIGNALING_RESULT_OK )
    {
        LogError( ( "Failed to construct Connect WSS Endpoint Request. Return=0x%x!", signalingResult ) );
        ret = SIGNALING_CONTROLLER_RESULT_FAIL;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        wssConnectInfo.pUrl = signalingRequest.pUrl;
        wssConnectInfo.urlLength = signalingRequest.urlLength;
        wssConnectInfo.rxCallback = OnWssMessageReceived;
        wssConnectInfo.pRxCallbackData = pCtx;

        awsCreds.pAccessKeyId = &( pCtx->accessKeyId[ 0 ] );;
        awsCreds.accessKeyIdLen = pCtx->accessKeyIdLength;

        awsCreds.pSecretAccessKey = &( pCtx->secretAccessKey[ 0 ] );
        awsCreds.secretAccessKeyLen = pCtx->secretAccessKeyLength;

        awsCreds.pSessionToken = &( pCtx->sessionToken[ 0 ] );
        awsCreds.sessionTokenLength = pCtx->sessionTokenLength;
        awsCreds.expirationSeconds = pCtx->expirationSeconds;

        networkingResult = Networking_WebsocketConnect( &( pCtx->websocketContext ),
                                                        &( wssConnectInfo ),
                                                        &( awsCreds ),
                                                        &( pCtx->awsConfig ) );

        if( networkingResult != NETWORKING_RESULT_OK )
        {
            LogError( ( "Failed to connect with WSS endpoint!" ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static SignalingControllerResult_t ConnectToSignalingService( SignalingControllerContext_t * pCtx,
                                                              const SignalingControllerConnectInfo_t * pConnectInfo )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    uint64_t currentTimeSeconds;

    if( ( pCtx == NULL ) || ( pConnectInfo == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        pCtx->pUserAgentName = pConnectInfo->pUserAgentName;
        pCtx->userAgentNameLength = pConnectInfo->userAgentNameLength;

        pCtx->awsConfig = pConnectInfo->awsConfig;

        pCtx->messageReceivedCallback = pConnectInfo->messageReceivedCallback;
        pCtx->pMessageReceivedCallbackData = pConnectInfo->pMessageReceivedCallbackData;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( ( pConnectInfo->awsIotCreds.thingNameLength > 0 ) &&
            ( pConnectInfo->awsIotCreds.roleAliasLength > 0 ) &&
            ( pConnectInfo->awsIotCreds.iotCredentialsEndpointLength > 0 ) )
        {
            currentTimeSeconds = NetworkingUtils_GetCurrentTimeSec( NULL );

            if( ( pCtx->expirationSeconds == 0 ) ||
                ( currentTimeSeconds >= pCtx->expirationSeconds - SIGNALING_CONTROLLER_FETCH_CREDS_GRACE_PERIOD_SEC ) )
            {
                Metric_StartEvent( METRIC_EVENT_SIGNALING_GET_CREDENTIALS );
                ret = FetchTemporaryCredentials( pCtx,
                                                 &( pConnectInfo->awsIotCreds ) );
                Metric_EndEvent( METRIC_EVENT_SIGNALING_GET_CREDENTIALS );
            }
        }
        else
        {
            /* The application must have supplied AWS credentials. */
            memcpy( &( pCtx->accessKeyId[ 0 ] ),
                    pConnectInfo->awsCreds.pAccessKeyId,
                    pConnectInfo->awsCreds.accessKeyIdLen );
            pCtx->accessKeyIdLength = pConnectInfo->awsCreds.accessKeyIdLen;

            memcpy( &( pCtx->secretAccessKey[ 0 ] ),
                    pConnectInfo->awsCreds.pSecretAccessKey,
                    pConnectInfo->awsCreds.secretAccessKeyLen );
            pCtx->secretAccessKeyLength = pConnectInfo->awsCreds.secretAccessKeyLen;

            memcpy( &( pCtx->sessionToken[ 0 ] ),
                    pConnectInfo->awsCreds.pSessionToken,
                    pConnectInfo->awsCreds.sessionTokenLength );
            pCtx->sessionTokenLength = pConnectInfo->awsCreds.sessionTokenLength;

            pCtx->expirationSeconds = pConnectInfo->awsCreds.expirationSeconds;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        Metric_StartEvent( METRIC_EVENT_SIGNALING_DESCRIBE_CHANNEL );
        ret = DescribeSignalingChannel( pCtx, &( pConnectInfo->channelName ) );
        Metric_EndEvent( METRIC_EVENT_SIGNALING_DESCRIBE_CHANNEL );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        Metric_StartEvent( METRIC_EVENT_SIGNALING_GET_ENDPOINTS );
        ret = GetSignalingChannelEndpoints( pCtx );
        Metric_EndEvent( METRIC_EVENT_SIGNALING_GET_ENDPOINTS );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        Metric_StartEvent( METRIC_EVENT_SIGNALING_GET_ICE_SERVER_LIST );
        ret = GetIceServerConfigs( pCtx );
        Metric_EndEvent( METRIC_EVENT_SIGNALING_GET_ICE_SERVER_LIST );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        Metric_StartEvent( METRIC_EVENT_SIGNALING_CONNECT_WSS_SERVER );
        ret = ConnectToWssEndpoint( pCtx );
        Metric_EndEvent( METRIC_EVENT_SIGNALING_CONNECT_WSS_SERVER );
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        LogSignalingInfo( pCtx );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static void LogSignalingInfo( SignalingControllerContext_t * pCtx )
{
    size_t i, j;

    LogInfo( ( "======================================== Channel Info ========================================" ) );
    LogInfo( ( "Signaling Channel ARN: %s", &( pCtx->signalingChannelArn[ 0 ] ) ) );

    LogInfo( ( "======================================== Endpoints Info ========================================" ) );
    LogInfo( ( "HTTPS Endpoint: %s", &( pCtx->httpsEndpoint[ 0 ] ) ) );
    LogInfo( ( "WSS Endpoint: %s", &( pCtx->wssEndpoint[ 0 ] ) ) );
    LogInfo( ( "WebRTC Endpoint: %s", pCtx->webrtcEndpointLength == 0 ? "N/A" : &( pCtx->webrtcEndpoint[ 0 ] ) ) );

    /* Ice server list */
    LogInfo( ( "======================================== Ice Server List ========================================" ) );
    LogInfo( ( "Ice Server Count: %lu", pCtx->iceServerConfigsCount ) );
    for( i = 0; i < pCtx->iceServerConfigsCount; i++ )
    {
        LogInfo( ( "======================================== Ice Server[%lu] ========================================", i ) );
        LogInfo( ( "    TTL (seconds): %u", pCtx->iceServerConfigs[ i ].ttlSeconds ) );
        LogInfo( ( "    User Name: %s", pCtx->iceServerConfigs[ i ].userName ) );
        LogInfo( ( "    Password: %s", pCtx->iceServerConfigs[ i ].password ) );
        LogInfo( ( "    URI Count: %lu", pCtx->iceServerConfigs[ i ].iceServerUriCount ) );

        for( j = 0; j < pCtx->iceServerConfigs[ i ].iceServerUriCount; j++ )
        {
            LogInfo( ( "        URI: %s", &( pCtx->iceServerConfigs[ i ].iceServerUris[ j ].uri[ 0 ] ) ) );
        }
    }
}

/*----------------------------------------------------------------------------*/

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t * pCtx,
                                                      const SSLCredentials_t * pSslCreds )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    NetworkingResult_t networkingResult;

    if( ( pCtx == NULL ) || ( pSslCreds == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        memset( pCtx, 0, sizeof( SignalingControllerContext_t ) );

        if( pthread_mutex_init( &( pCtx->signalingTxMutex ), NULL ) != 0 )
        {
            LogError( ( "Failed to initialize signalingTxMutex!" ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        networkingResult = Networking_HttpInit( &( pCtx->httpContext ),
                                                pSslCreds );

        if( networkingResult != NETWORKING_RESULT_OK )
        {
            LogError( ( "Failed to initialize http!" ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        networkingResult = Networking_WebsocketInit( &( pCtx->websocketContext ),
                                                     pSslCreds );

        if( networkingResult != NETWORKING_RESULT_OK )
        {
            LogError( ( "Failed to initialize websocket!" ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

SignalingControllerResult_t SignalingController_StartListening( SignalingControllerContext_t * pCtx,
                                                                const SignalingControllerConnectInfo_t * pConnectInfo )
{
    SignalingControllerResult_t ret;
    NetworkingResult_t networkingResult;

    for( ;; )
    {
        ret = SIGNALING_CONTROLLER_RESULT_OK;
        networkingResult = NETWORKING_RESULT_OK;

        ret = ConnectToSignalingService( pCtx, pConnectInfo );

        if( ret == SIGNALING_CONTROLLER_RESULT_OK )
        {
            while( networkingResult == NETWORKING_RESULT_OK )
            {
                networkingResult = Networking_WebsocketSignal( &( pCtx->websocketContext ) );
            }
        }
        else
        {
            LogError( ( "Failed to connect to signaling service. Result: %d!", ret ) );
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t * pCtx,
                                                             const SignalingMessage_t * pSignalingMessage )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    Base64Result_t base64Result;
    WssSendMessage_t wssSendMessage;
    SignalingResult_t signalingResult;
    NetworkingResult_t networkingResult;

    if( ( pCtx == NULL ) ||
        ( pSignalingMessage == NULL ) ||
        ( pSignalingMessage->pMessage == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        pthread_mutex_lock( &( pCtx->signalingTxMutex ) );
        {
            LogDebug( ( "Sending signaling message(%lu): %.*s",
                        pSignalingMessage->messageLength,
                        ( int ) pSignalingMessage->messageLength,
                        pSignalingMessage->pMessage ) );

            pCtx->signalingIntermediateMessageLength = SIGNALING_CONTROLLER_MESSAGE_BUFFER_LENGTH;
            base64Result = Base64_Encode( pSignalingMessage->pMessage,
                                          pSignalingMessage->messageLength,
                                          &( pCtx->signalingIntermediateMessageBuffer[ 0 ] ),
                                          &( pCtx->signalingIntermediateMessageLength ) );

            if( base64Result != BASE64_RESULT_OK )
            {
                LogError( ( "Failed to base64 encode signaling message. Result: %d!", base64Result ) );
                ret = SIGNALING_CONTROLLER_RESULT_FAIL;
            }


            if( ret == SIGNALING_CONTROLLER_RESULT_OK )
            {
                memset( &( wssSendMessage ), 0, sizeof( WssSendMessage_t ) );

                wssSendMessage.messageType = pSignalingMessage->messageType;
                wssSendMessage.pBase64EncodedMessage = &( pCtx->signalingIntermediateMessageBuffer[ 0 ] );
                wssSendMessage.base64EncodedMessageLength = pCtx->signalingIntermediateMessageLength;
                wssSendMessage.pCorrelationId = pSignalingMessage->pCorrelationId;
                wssSendMessage.correlationIdLength = pSignalingMessage->correlationIdLength;
                wssSendMessage.pRecipientClientId = pSignalingMessage->pRemoteClientId;
                wssSendMessage.recipientClientIdLength = pSignalingMessage->remoteClientIdLength;

                pCtx->signalingTxMessageLength = SIGNALING_CONTROLLER_MESSAGE_BUFFER_LENGTH;
                signalingResult = Signaling_ConstructWssMessage( &( wssSendMessage ),
                                                                 &( pCtx->signalingTxMessageBuffer[ 0 ] ),
                                                                 &( pCtx->signalingTxMessageLength ) );

                if( signalingResult != SIGNALING_RESULT_OK )
                {
                    LogError( ( "Failed to construct signaling Wss message. Result: %d!", signalingResult ) );
                    ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                }
            }

            if( ret == SIGNALING_CONTROLLER_RESULT_OK )
            {
                LogVerbose( ( "Constructed signaling WSS message (%lu): \n%.*s",
                              pCtx->signalingTxMessageLength,
                              ( int ) pCtx->signalingTxMessageLength,
                              &( pCtx->signalingTxMessageBuffer[ 0 ] ) ) );

                networkingResult = Networking_WebsocketSend( &( pCtx->websocketContext ),
                                                             &( pCtx->signalingTxMessageBuffer[ 0 ] ),
                                                             pCtx->signalingTxMessageLength );

                if( networkingResult != NETWORKING_RESULT_OK )
                {
                    LogError( ( "Failed to send signaling Wss message. Result: %d!", networkingResult ) );
                    ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                }
            }
        }
        pthread_mutex_unlock( &( pCtx->signalingTxMutex ) );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

SignalingControllerResult_t SignalingController_QueryIceServerConfigs( SignalingControllerContext_t * pCtx,
                                                                       IceServerConfig_t ** ppIceServerConfigs,
                                                                       size_t * pIceServerConfigsCount )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    uint64_t currentTimeSec;

    if( ( pCtx == NULL ) ||
        ( ppIceServerConfigs == NULL ) ||
        ( pIceServerConfigsCount == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        currentTimeSec = NetworkingUtils_GetCurrentTimeSec( NULL );

        if( ( pCtx->iceServerConfigsCount == 0 ) ||
            ( pCtx->iceServerConfigExpirationSec < currentTimeSec ) )
        {
            LogInfo( ( "Ice server configs expired. Refresing Configs." ) );

            Metric_StartEvent( METRIC_EVENT_SIGNALING_GET_ICE_SERVER_LIST );
            ret = GetIceServerConfigs( pCtx );
            Metric_EndEvent( METRIC_EVENT_SIGNALING_GET_ICE_SERVER_LIST );
        }

        *ppIceServerConfigs = pCtx->iceServerConfigs;
        *pIceServerConfigsCount = pCtx->iceServerConfigsCount;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

SignalingControllerResult_t SignalingController_RefreshIceServerConfigs( SignalingControllerContext_t * pCtx )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;

    if( pCtx == NULL )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        ret = GetIceServerConfigs( pCtx );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

SignalingControllerResult_t SignalingController_ExtractSdpOfferFromSignalingMessage( const char * pSignalingMessage,
                                                                                     size_t signalingMessageLength,
                                                                                     const char ** ppSdpMessage,
                                                                                     size_t * pSdpMessageLength )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    JSONStatus_t jsonResult;
    size_t start = 0, next = 0;
    JSONPair_t pair = { 0 };
    uint8_t sdpOfferFound = 0;

    if( ( pSignalingMessage == NULL ) ||
        ( signalingMessageLength == 0 ) ||
        ( ppSdpMessage == NULL ) ||
        ( pSdpMessageLength == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        jsonResult = JSON_Validate( pSignalingMessage, signalingMessageLength );

        if( jsonResult != JSONSuccess )
        {
            LogError( ( "Signaling message is not valid JSON. Result: %d, message(%lu): %.*s!",
                        jsonResult,
                        signalingMessageLength,
                        ( int ) signalingMessageLength,
                        pSignalingMessage ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        jsonResult = JSON_Iterate( pSignalingMessage, signalingMessageLength, &( start ), &( next ), &( pair ) );

        while( jsonResult == JSONSuccess )
        {
            if( ( strncmp( pair.key, "type", pair.keyLength ) == 0 ) &&
                ( strncmp( pair.value, "offer", pair.valueLength ) != 0 ) )
            {
                LogError( ( "Message type \"%.*s\" is not SDP offer!",
                            ( int ) pair.valueLength,
                            pair.value ) );

                ret = SIGNALING_CONTROLLER_RESULT_FAIL;

                break;
            }
            else if( strncmp( pair.key, "sdp", pair.keyLength ) == 0 )
            {
                *ppSdpMessage = pair.value;
                *pSdpMessageLength = pair.valueLength;
                sdpOfferFound = 1;

                break;
            }
            else
            {
                /* Skip unknown attributes. */
            }

            jsonResult = JSON_Iterate( pSignalingMessage, signalingMessageLength, &( start ), &( next ), &( pair ) );
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && ( sdpOfferFound == 0 ) )
    {
        LogError( ( "SDP offer not found in signaling message(%lu): %.*s",
                    signalingMessageLength,
                    ( int ) signalingMessageLength,
                    pSignalingMessage ) );

        ret = SIGNALING_CONTROLLER_RESULT_FAIL;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

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
        ( sdpMessageLength == 0 ) ||
        ( pFormalSdpMessage == NULL ) ||
        ( pFormalSdpMessageLength == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        pCurOutput = pFormalSdpMessage;

        while( ( pNext = strstr( pCurSdp, "\\n" ) ) != NULL )
        {
            lineLength = pNext - pCurSdp;

            if( ( lineLength >= 2 ) &&
                ( pCurSdp[ lineLength - 2 ] == '\\' ) && ( pCurSdp[ lineLength - 1 ] == 'r' ) )
            {
                lineLength -= 2;
            }

            if( *pFormalSdpMessageLength < outputLength + lineLength + 2 )
            {
                LogError( ( "Buffer space is not enough to store formal SDP message, buffer size: %lu, SDP message(%lu): %.*s",
                          *pFormalSdpMessageLength,
                          sdpMessageLength,
                          ( int ) sdpMessageLength,
                          pSdpMessage ) );

                ret = SIGNALING_CONTROLLER_RESULT_FAIL;
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

/*----------------------------------------------------------------------------*/

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
        ( sdpMessageLength == 0 ) ||
        ( pEventSdpMessage == NULL ) ||
        ( pEventSdpMessageLength == NULL ) )
    {
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
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

            if( *pEventSdpMessageLength < outputLength + lineLength + 4 )
            {
                LogError( ( "The output buffer length(%lu) is too small to store serialized %lu bytes message.",
                            *pEventSdpMessageLength,
                            sdpMessageLength ) );

                ret = SIGNALING_CONTROLLER_RESULT_FAIL;

                break;
            }

            writtenLength = snprintf( pCurOutput, *pEventSdpMessageLength - outputLength, "%.*s\\r\\n",
                                      ( int ) lineLength,
                                      pCurSdp );

            if( writtenLength < 0 )
            {
                ret = SIGNALING_CONTROLLER_RESULT_FAIL;

                LogError( ( "snprintf failed with error %d!", writtenLength ) );

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

/*----------------------------------------------------------------------------*/
