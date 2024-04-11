#include "networkingLibwebsockets.h"
#include "networkingLibwebsockets_private.h"

#define NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME  "X-Amz-ChannelARN"

static NetworkingLibwebsocketsResult_t generateQueryParametersString( char *pQueryStart, size_t queryLength, char *pOutput, size_t *pOutputLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char *pChannelArnQueryParam, *pChannelArnValue, *pEqual;
    size_t channelArnQueryParamLength, channelArnValueLength;

    if( pOutput == NULL || pOutputLength == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        if( queryLength < strlen( NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME ) || 
            strncmp( pQueryStart, NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME, strlen( NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME ) ) != 0 )
        {
            /* No channel ARN exist. */
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_UNEXPECTED_WEBSOCKET_URL;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        /* Parse existing query parameters. */
        pChannelArnQueryParam = pQueryStart;
        pEqual = strchr( pQueryStart, '=' );
        if( pEqual == NULL )
        {
            /* No equal found, unexpected. */
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_UNEXPECTED_WEBSOCKET_URL;
        }
        else
        {
            channelArnQueryParamLength = pEqual - pQueryStart;
            pChannelArnValue = pEqual + 1;
            channelArnValueLength = pQueryStart + queryLength - pChannelArnValue;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        /* Put all parameters, including signing parameters, to output buffer. */

    }

    return ret;
}

static NetworkingLibwebsocketsResult_t signWebsocketRequest( WebsocketServerInfo_t *pWebsocketServerInfo )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    // int32_t writtenLength;
    // NetworkingLibwebsocketsAppendHeaders_t *pAppendHeaders = &networkingLibwebsocketContext.appendHeaders;
    // NetworkingLibwebsocketCanonicalRequest_t canonicalRequest;
    // char *pPath, *pQueryStart, *pUrlEnd;
    // size_t pathLength, queryLength;
    // size_t queryParamsStringLength;
    
    // /* Find the path for request. */
    // ret = getPathFromUrl( pWebsocketServerInfo->pUrl, pWebsocketServerInfo->urlLength, &pPath, &pathLength );

    // if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    // {
    //     pUrlEnd = pWebsocketServerInfo->pUrl + pWebsocketServerInfo->urlLength;
    //     pQueryStart = pPath + pathLength;
    //     queryLength = pUrlEnd - pQueryStart;

    //     if( queryLength <= 0 )
    //     {
    //         ret = NETWORKING_LIBWEBSOCKETS_RESULT_UNEXPECTED_WEBSOCKET_URL;
    //     }
    // }

    // if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    // {
    //     /* Follow https://docs.aws.amazon.com/IAM/latest/UserGuide/create-signed-request.html to create query parameters. */
    //     queryParamsStringLength = NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH;
    //     ret = generateQueryParametersString( pQueryStart,
    //                                          queryLength,
    //                                          networkingLibwebsocketContext.sigv4Metadatabuffer,
    //                                          &queryParamsStringLength );
    // }

    // if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    // {
    //     writtenLength = NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH - queryParamsStringLength;

    //     ret = uriEncodedQueryParametersString( networkingLibwebsocketContext.sigv4Metadatabuffer,
    //                                            queryParamsStringLength,
    //                                            networkingLibwebsocketContext.sigv4Metadatabuffer + queryParamsStringLength,
    //                                            &writtenLength );
    // }

    return ret;
}

int32_t lwsWebsocketCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize)
{
    int32_t retValue = 0;
    int32_t status;

    printf( "Websocket callback with reason %d\n", reason );
    
    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            // pCurPtr = pDataIn == NULL ? "(None)" : (PCHAR) pDataIn;
            // DLOGW("Client connection failed. Connection error string: %s", pCurPtr);
            // STRNCPY(pLwsCallInfo->callInfo.errorBuffer, pCurPtr, CALL_INFO_ERROR_BUFFER_LEN);

            // // TODO: Attempt to get more meaningful service return code

            // ATOMIC_STORE_BOOL(&pRequestInfo->terminating, TRUE);
            // connected = ATOMIC_EXCHANGE_BOOL(&pSignalingClient->connected, FALSE);

            // CVAR_BROADCAST(pSignalingClient->receiveCvar);
            // CVAR_BROADCAST(pSignalingClient->sendCvar);
            // ATOMIC_STORE(&pSignalingClient->messageResult, (SIZE_T) SERVICE_CALL_UNKNOWN);
            // ATOMIC_STORE(&pSignalingClient->result, (SIZE_T) SERVICE_CALL_UNKNOWN);

            // if (connected && !ATOMIC_LOAD_BOOL(&pSignalingClient->shutdown)) {
            //     // Handle re-connection in a reconnect handler thread. Set the terminated indicator before the thread
            //     // creation and the thread itself will reset it. NOTE: Need to check for a failure and reset.
            //     ATOMIC_STORE_BOOL(&pSignalingClient->reconnecterTracker.terminated, FALSE);
            //     retStatus = THREAD_CREATE(&pSignalingClient->reconnecterTracker.threadId, reconnectHandler, (PVOID) pSignalingClient);
            //     if (STATUS_FAILED(retStatus)) {
            //         ATOMIC_STORE_BOOL(&pSignalingClient->reconnecterTracker.terminated, TRUE);
            //         CHK(FALSE, retStatus);
            //     }

            //     CHK_STATUS(THREAD_DETACH(pSignalingClient->reconnecterTracker.threadId));
            // }

            break;
        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
            printf( "Client HTTP established" );
            status = lws_http_client_http_response(wsi);
            printf( "Connected with server response: %d\n", status );

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf( "Connection established" );
            status = lws_http_client_http_response(wsi);
            // getStateMachineCurrentState(pSignalingClient->pStateMachine, &pStateMachineState);

            printf( "Connected with server response: %d\n", status );

            // // Set the call result to succeeded
            // ATOMIC_STORE(&pSignalingClient->result, (SIZE_T) SERVICE_CALL_RESULT_OK);
            // ATOMIC_STORE_BOOL(&pSignalingClient->connected, TRUE);

            // // Store the time when we connect for diagnostics
            // MUTEX_LOCK(pSignalingClient->diagnosticsLock);
            // pSignalingClient->diagnostics.connectTime = SIGNALING_GET_CURRENT_TIME(pSignalingClient);
            // MUTEX_UNLOCK(pSignalingClient->diagnosticsLock);

            // // Notify the listener thread
            // CVAR_BROADCAST(pSignalingClient->connectedCvar);

            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            printf( "Client WSS closed" );

            // ATOMIC_STORE_BOOL(&pRequestInfo->terminating, TRUE);
            // connected = ATOMIC_EXCHANGE_BOOL(&pSignalingClient->connected, FALSE);

            // CVAR_BROADCAST(pSignalingClient->receiveCvar);
            // CVAR_BROADCAST(pSignalingClient->sendCvar);
            // ATOMIC_STORE(&pSignalingClient->messageResult, (SIZE_T) SERVICE_CALL_UNKNOWN);

            // if (connected && ATOMIC_LOAD(&pSignalingClient->result) != SERVICE_CALL_RESULT_SIGNALING_RECONNECT_ICE &&
            //     !ATOMIC_LOAD_BOOL(&pSignalingClient->shutdown)) {
            //     // Set the result failed
            //     ATOMIC_STORE(&pSignalingClient->result, (SIZE_T) SERVICE_CALL_UNKNOWN);

            //     // Handle re-connection in a reconnect handler thread. Set the terminated indicator before the thread
            //     // creation and the thread itself will reset it. NOTE: Need to check for a failure and reset.
            //     ATOMIC_STORE_BOOL(&pSignalingClient->reconnecterTracker.terminated, FALSE);
            //     retStatus = THREAD_CREATE(&pSignalingClient->reconnecterTracker.threadId, reconnectHandler, (PVOID) pSignalingClient);
            //     if (STATUS_FAILED(retStatus)) {
            //         ATOMIC_STORE_BOOL(&pSignalingClient->reconnecterTracker.terminated, TRUE);
            //         CHK(FALSE, retStatus);
            //     }
            //     CHK_STATUS(THREAD_DETACH(pSignalingClient->reconnecterTracker.threadId));
            // }

            break;

        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
            // status = 0;
            // pCurPtr = NULL;
            // size = (UINT32) dataSize;
            // if (dataSize > SIZEOF(UINT16)) {
            //     // The status should be the first two bytes in network order
            //     status = getInt16(*(PINT16) pDataIn);

            //     // Set the string past the status
            //     pCurPtr = (PCHAR) ((PBYTE) pDataIn + SIZEOF(UINT16));
            //     size -= SIZEOF(UINT16);
            // }

            // DLOGD("Peer initiated close with %d (0x%08x). Message: %.*s", status, (UINT32) status, size, pCurPtr);

            // // Store the state as the result
            // retValue = -1;

            // ATOMIC_STORE(&pSignalingClient->result, (SIZE_T) status);

            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:

            // Check if it's a binary data
            // CHK(!lws_frame_is_binary(wsi), STATUS_SIGNALING_RECEIVE_BINARY_DATA_NOT_SUPPORTED);

            // // Skip if it's the first and last fragment and the size is 0
            // CHK(!(lws_is_first_fragment(wsi) && lws_is_final_fragment(wsi) && dataSize == 0), retStatus);

            // // Check what type of a message it is. We will set the size to 0 on first and flush on last
            // if (lws_is_first_fragment(wsi)) {
            //     pLwsCallInfo->receiveBufferSize = 0;
            // }

            // // Store the data in the buffer
            // CHK(pLwsCallInfo->receiveBufferSize + (UINT32) dataSize + LWS_PRE <= SIZEOF(pLwsCallInfo->receiveBuffer),
            //     STATUS_SIGNALING_RECEIVED_MESSAGE_LARGER_THAN_MAX_DATA_LEN);
            // MEMCPY(&pLwsCallInfo->receiveBuffer[LWS_PRE + pLwsCallInfo->receiveBufferSize], pDataIn, dataSize);
            // pLwsCallInfo->receiveBufferSize += (UINT32) dataSize;

            // // Flush on last
            // if (lws_is_final_fragment(wsi)) {
            //     CHK_STATUS(receiveLwsMessage(pLwsCallInfo->pSignalingClient, (PCHAR) &pLwsCallInfo->receiveBuffer[LWS_PRE],
            //                                  pLwsCallInfo->receiveBufferSize / SIZEOF(CHAR)));
            // }

            // lws_callback_on_writable(wsi);

            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            // DLOGD("Client is writable");

            // // Check if we are attempting to terminate the connection
            // if (!ATOMIC_LOAD_BOOL(&pSignalingClient->connected) && ATOMIC_LOAD(&pSignalingClient->messageResult) == SERVICE_CALL_UNKNOWN) {
            //     retValue = 1;
            //     CHK(FALSE, retStatus);
            // }

            // offset = (UINT32) ATOMIC_LOAD(&pLwsCallInfo->sendOffset);
            // bufferSize = (UINT32) ATOMIC_LOAD(&pLwsCallInfo->sendBufferSize);
            // writeSize = (INT32) (bufferSize - offset);

            // // Check if we need to do anything
            // CHK(writeSize > 0, retStatus);

            // // Send data and notify on completion
            // size = lws_write(wsi, &(pLwsCallInfo->sendBuffer[pLwsCallInfo->sendOffset]), (SIZE_T) writeSize, LWS_WRITE_TEXT);

            // if (size < 0) {
            //     DLOGW("Write failed. Returned write size is %d", size);
            //     // Quit
            //     retValue = -1;
            //     CHK(FALSE, retStatus);
            // }

            // if (size == writeSize) {
            //     // Notify the listener
            //     ATOMIC_STORE(&pLwsCallInfo->sendOffset, 0);
            //     ATOMIC_STORE(&pLwsCallInfo->sendBufferSize, 0);
            //     CVAR_BROADCAST(pLwsCallInfo->pSignalingClient->sendCvar);
            // } else {
            //     // Partial write
            //     DLOGV("Failed to write out the data entirely. Wrote %d out of %d", size, writeSize);
            //     // Schedule again
            //     lws_callback_on_writable(wsi);
            // }

            break;

        default:
            break;
    }

    return retValue;
}

WebsocketResult_t Websocket_Connect( WebsocketServerInfo_t * pServerInfo )
{
    WebsocketResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char *pPath;
    size_t pathLength;

    /* Append HTTP headers for signing.
     * Refer to https://docs.aws.amazon.com/AmazonECR/latest/APIReference/CommonParameters.html for details. */
    /* user-agent */
    networkingLibwebsocketContext.appendHeaders.pUserAgent = networkingLibwebsocketContext.libwebsocketsCredentials.pUserAgent;
    networkingLibwebsocketContext.appendHeaders.userAgentLength = networkingLibwebsocketContext.libwebsocketsCredentials.userAgentLength;

    /* host */
    ret = getUrlHost( pServerInfo->pUrl, pServerInfo->urlLength, &networkingLibwebsocketContext.appendHeaders.pHost, &networkingLibwebsocketContext.appendHeaders.hostLength );

    /* x-amz-date */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = getIso8601CurrentTime( &networkingLibwebsocketContext.appendHeaders.pDate, &networkingLibwebsocketContext.appendHeaders.dateLength );
    }

    /* 
        wss://m-d73cdb00.kinesisvideo.us-west-2.amazonaws.com?
        X-Amz-Algorithm=AWS4-HMAC-SHA256&
        X-Amz-ChannelARN=arn%3Aaws%3Akinesisvideo%3Aus-west-2%3A767859759493%3Achannel%2FtestSignalChannelUS%2F1692761105940&
        X-Amz-Credential=AKIA3FSACDGC5A2V4ICB%2F20240410%2Fus-west-2%2Fkinesisvideo%2Faws4_request&
        X-Amz-Date=20240410T033722Z&
        X-Amz-Expires=604800&
        X-Amz-SignedHeaders=host&
        X-Amz-Signature=4b71a1b835e4a30275bd98543373404a79bff83b710f123bd54ced9e75cf4e45
    */

    /* Store path into path buffer. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = getPathFromUrl( pServerInfo->pUrl, pServerInfo->urlLength, &pPath, &pathLength );
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        if( pathLength >= NETWORKING_LWS_MAX_URI_LENGTH )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_PATH_BUFFER_TOO_SMALL;
        }
        else
        {
            memcpy( &networkingLibwebsocketContext.pathBuffer[1], pPath, pathLength );
            networkingLibwebsocketContext.pathBuffer[ pathLength + 1 ] = '\0';
            networkingLibwebsocketContext.pathBuffer[ 0 ] = '/';
            networkingLibwebsocketContext.pathBufferWrittenLength = pathLength;
        }
    }

    /* Create query parameters. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = signWebsocketRequest( pServerInfo );
    }

    /* Blocking execution until getting response from server. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = performLwsConnect( networkingLibwebsocketContext.appendHeaders.pHost, networkingLibwebsocketContext.appendHeaders.hostLength, 443U, 0U );
    }

    return ret;
}

WebsocketResult_t Websocket_Init( void * pCredential )
{
    NetworkingLibwebsocketsCredentials_t *pNetworkingLibwebsocketsCredentials = (NetworkingLibwebsocketsCredentials_t *)pCredential;

    return NetworkingLibwebsockets_Init( pNetworkingLibwebsocketsCredentials );
}
