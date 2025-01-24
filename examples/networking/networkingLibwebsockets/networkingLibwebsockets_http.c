#include "logging.h"
#include "networkingLibwebsockets.h"
#include "networkingLibwebsockets_private.h"
#include "libwebsockets.h"

static NetworkingLibwebsocketsResult_t SignHttpRequest( HttpRequest_t * pHttpRequest )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    int32_t writtenLength;
    NetworkingLibwebsocketsAppendHeaders_t * pAppendHeaders = &networkingLibwebsocketContext.appendHeaders;
    NetworkingLibwebsocketCanonicalRequest_t canonicalRequest;
    char * pPath;
    size_t pathLength;

    /* Follow https://docs.aws.amazon.com/IAM/latest/UserGuide/create-signed-request.html to create canonical headers.
     * HTTP Format: "host: kinesisvideo.us-west-2.amazonaws.com\r\nuser-agent: AWS-SDK-KVS\r\n..."
     *
     * Note that we re-use the parsed result in pAppendHeaders from Http_Send(). */
    writtenLength = snprintf( networkingLibwebsocketContext.sigv4Metadatabuffer, NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH, "%s: %.*s\r\n%s: %.*s\r\n%s: %.*s\r\n",
                              "host", ( int ) pAppendHeaders->hostLength, pAppendHeaders->pHost,
                              "user-agent", ( int ) pAppendHeaders->userAgentLength, pAppendHeaders->pUserAgent,
                              "x-amz-date", ( int ) pAppendHeaders->dateLength, pAppendHeaders->pDate );

    if( writtenLength < 0 )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
    }
    else if( writtenLength == NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_AUTH_BUFFER_TOO_SMALL;
    }
    else
    {
        /* Do nothing, Coverity happy. */
    }

    /* Find the path for request. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = GetPathFromUrl( pHttpRequest->pUrl, pHttpRequest->urlLength, &pPath, &pathLength );
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        if( pathLength > NETWORKING_LWS_MAX_URI_LENGTH )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_PATH_BUFFER_TOO_SMALL;
        }
        else
        {
            memcpy( networkingLibwebsocketContext.pathBuffer, pPath, pathLength );
            networkingLibwebsocketContext.pathBuffer[ pathLength ] = '\0';
            networkingLibwebsocketContext.pathBuffer[ 0 ] = '/';
            networkingLibwebsocketContext.pathBufferWrittenLength = pathLength;
        }
    }

    /* While fetching credential, we don't need to generate authorization header and we don't have the key pair at this moment. */
    if( ( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK ) && ( pHttpRequest->isFetchingCredential == 0U ) )
    {
        memset( &canonicalRequest, 0, sizeof( canonicalRequest ) );
        canonicalRequest.verb = NETWORKING_LWS_HTTP_VERB_POST;
        canonicalRequest.pPath = pPath;
        canonicalRequest.pathLength = pathLength;
        canonicalRequest.pCanonicalQueryString = NULL;
        canonicalRequest.canonicalQueryStringLength = 0U;
        canonicalRequest.pCanonicalHeaders = networkingLibwebsocketContext.sigv4Metadatabuffer;
        canonicalRequest.canonicalHeadersLength = writtenLength;
        canonicalRequest.pPayload = pHttpRequest->pBody;
        canonicalRequest.payloadLength = pHttpRequest->bodyLength;

        ret = GenerateAuthorizationHeader( &canonicalRequest );
    }

    return ret;
}

int32_t LwsHttpCallbackRoutine( struct lws * wsi,
                                enum lws_callback_reasons reason,
                                void * pUser,
                                void * pDataIn,
                                size_t dataSize )
{
    int32_t retValue = 0;
    int32_t status, writtenBodyLength, readLength;
    void * pCustomData;
    char * pCurPtr;
    // char dateHdrBuffer[MAX_DATE_HEADER_BUFFER_LENGTH + 1];
    char * pEndPtr;
    char ** ppStartPtr;
    uint64_t item, serverTime;
    uint32_t headerCount;
    uint32_t logLevel;
    time_t td;
    size_t len;
    uint64_t nowTime, clockSkew = 0;
    NetworkingLibwebsocketContext_t * pCtx;

    char pContentLength[ 11 ]; /* It needs 10 bytes for 32 bit integer, +1 for NULL terminator. */
    size_t contentWrittenLength;

    ( void ) pUser;

    LogVerbose( ( "HTTP callback with reason %d ", reason ) );

    switch( reason ) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            // pCurPtr = pDataIn == NULL ? "(None)" : (PCHAR) pDataIn;
            // DLOGW("Client connection failed. Connection error string: %s", pCurPtr);
            // STRNCPY(pLwsCallInfo->callInfo.errorBuffer, pCurPtr, CALL_INFO_ERROR_BUFFER_LEN);

            // // TODO: Attempt to get more meaningful service return code

            // ATOMIC_STORE_BOOL(&pRequestInfo->terminating, TRUE);
            // ATOMIC_STORE(&pLwsCallInfo->pSignalingClient->result, (SIZE_T) SERVICE_CALL_UNKNOWN);

            break;

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
            // DLOGD("Client http closed");
            // ATOMIC_STORE_BOOL(&pRequestInfo->terminating, TRUE);

            break;

        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
            status = lws_http_client_http_response( wsi );
            // getStateMachineCurrentState(pSignalingClient->pStateMachine, &pStateMachineState);

            LogInfo( ( "Connected with server response: %d ", status ) );
            // pLwsCallInfo->callInfo.callResult = getServiceCallResultFromHttpStatus((UINT32) status);

            // len = (SIZE_T) lws_hdr_copy(wsi, &dateHdrBuffer[0], MAX_DATE_HEADER_BUFFER_LENGTH, WSI_TOKEN_HTTP_DATE);

            // time(&td);

            // if (len) {
            //     // on failure to parse lws_http_date_unix returns non zero value
            //     if (0 == lws_http_date_parse_unix(&dateHdrBuffer[0], len, &td)) {
            //         DLOGV("Date Header Returned By Server:  %s", dateHdrBuffer);

            //         serverTime = ((UINT64) td) * HUNDREDS_OF_NANOS_IN_A_SECOND;

            //         if (serverTime > nowTime + MIN_CLOCK_SKEW_TIME_TO_CORRECT) {
            //             // Server time is ahead
            //             clockSkew = (serverTime - nowTime);
            //             DLOGD("Detected Clock Skew!  Server time is AHEAD of Device time: Server time: %" PRIu64 ", now time: %" PRIu64, serverTime,
            //                   nowTime);
            //         } else if (nowTime > serverTime + MIN_CLOCK_SKEW_TIME_TO_CORRECT) {
            //             clockSkew = (nowTime - serverTime);
            //             clockSkew |= ((UINT64) (1ULL << 63));
            //             DLOGD("Detected Clock Skew!  Device time is AHEAD of Server time: Server time: %" PRIu64 ", now time: %" PRIu64, serverTime,
            //                   nowTime);
            //             // PIC hashTable implementation only stores UINT64 so I will flip the sign of the msb
            //             // This limits the range of the max clock skew we can represent to just under 2925 years.
            //         }

            //         hashTableContains(pSignalingClient->diagnostics.pEndpointToClockSkewHashMap, pStateMachineState->state, &skewMapContains);
            //         if (clockSkew > 0) {
            //             hashTablePut(pSignalingClient->diagnostics.pEndpointToClockSkewHashMap, pStateMachineState->state, clockSkew);
            //         } else if (clockSkew == 0 && skewMapContains) {
            //             // This means the item is in the map so at one point there was a clock skew offset but it has been corrected
            //             // So we should no longer be correcting for a clock skew, remove this item from the map
            //             hashTableRemove(pSignalingClient->diagnostics.pEndpointToClockSkewHashMap, pStateMachineState->state);
            //         }
            //     }
            // }

            // // Store the Request ID header
            // if ((size = lws_hdr_custom_copy(wsi, pBuffer, LWS_SCRATCH_BUFFER_SIZE, SIGNALING_REQUEST_ID_HEADER_NAME,
            //                                 (SIZEOF(SIGNALING_REQUEST_ID_HEADER_NAME) - 1) * SIZEOF(CHAR))) > 0) {
            //     pBuffer[size] = '\0';
            //     DLOGI("Request ID: %s", pBuffer);
            // }

            break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
            LogInfo( ( "Received client http read: %lu bytes ", dataSize ) );
            lwsl_hexdump_debug( pDataIn, dataSize );

            if( dataSize != 0 )
            {
                if( dataSize >= networkingLibwebsocketContext.pResponse->bufferLength )
                {
                    /* Receive data is larger than buffer size. */
                    retValue = -2;
                }
                else
                {
                    memcpy( networkingLibwebsocketContext.pResponse->pBuffer, pDataIn, dataSize );
                    networkingLibwebsocketContext.pResponse->pBuffer[dataSize] = '\0';
                    networkingLibwebsocketContext.pResponse->bufferLength = dataSize;

                    LogDebug( ( "%s ", networkingLibwebsocketContext.pResponse->pBuffer ) );
                }

                // if (pLwsCallInfo->callInfo.callResult != SERVICE_CALL_RESULT_OK) {
                //     DLOGW("Received client http read response:  %s", pLwsCallInfo->callInfo.responseData);
                //     if (pLwsCallInfo->callInfo.callResult == SERVICE_CALL_FORBIDDEN) {
                //         if (isCallResultSignatureExpired(&pLwsCallInfo->callInfo)) {
                //             // Set more specific result, this is so in the state machine
                //             // We don't call GetToken again rather RETRY the existing API (now with clock skew correction)
                //             pLwsCallInfo->callInfo.callResult = SERVICE_CALL_SIGNATURE_EXPIRED;
                //         } else if (isCallResultSignatureNotYetCurrent(&pLwsCallInfo->callInfo)) {
                //             // server time is ahead
                //             pLwsCallInfo->callInfo.callResult = SERVICE_CALL_SIGNATURE_NOT_YET_CURRENT;
                //         }
                //     }
                // } else {
                //     DLOGV("Received client http read response:  %s", pLwsCallInfo->callInfo.responseData);
                // }
            }

            break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
            readLength = NETWORKING_LWS_MAX_BUFFER_LENGTH;

            if( lws_http_client_read( wsi, ( char ** )networkingLibwebsocketContext.pLwsBuffer, &readLength ) < 0 )
            {
                retValue = -1;
            }

            break;

        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
            // DLOGD("Http client completed");
            break;

        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
            ppStartPtr = ( char ** ) pDataIn;
            pEndPtr = *ppStartPtr + dataSize - 1;

            // Append headers
            LogDebug( ( "Appending header - Authorization=%.*s ", ( int ) networkingLibwebsocketContext.appendHeaders.authorizationLength, networkingLibwebsocketContext.appendHeaders.pAuthorization ) );
            status = lws_add_http_header_by_name( wsi, "Authorization", networkingLibwebsocketContext.appendHeaders.pAuthorization, networkingLibwebsocketContext.appendHeaders.authorizationLength,
                                                  ( unsigned char ** ) ppStartPtr, pEndPtr );

            /* user-agent */
            LogDebug( ( "Appending header - user-agent=%.*s ", ( int ) networkingLibwebsocketContext.appendHeaders.userAgentLength, networkingLibwebsocketContext.appendHeaders.pUserAgent ) );
            status = lws_add_http_header_by_name( wsi, "user-agent", networkingLibwebsocketContext.appendHeaders.pUserAgent, networkingLibwebsocketContext.appendHeaders.userAgentLength,
                                                  ( unsigned char ** ) ppStartPtr, pEndPtr );

            LogDebug( ( "Appending header - x-amz-date=%.*s ", ( int ) networkingLibwebsocketContext.appendHeaders.dateLength, networkingLibwebsocketContext.appendHeaders.pDate ) );
            status = lws_add_http_header_by_name( wsi, "x-amz-date", networkingLibwebsocketContext.appendHeaders.pDate, networkingLibwebsocketContext.appendHeaders.dateLength,
                                                  ( unsigned char ** ) ppStartPtr, pEndPtr );

            LogDebug( ( "Appending header - content-type=%.*s ", ( int ) networkingLibwebsocketContext.appendHeaders.contentTypeLength, networkingLibwebsocketContext.appendHeaders.pContentType ) );
            status = lws_add_http_header_by_name( wsi, "content-type", networkingLibwebsocketContext.appendHeaders.pContentType, networkingLibwebsocketContext.appendHeaders.contentTypeLength,
                                                  ( unsigned char ** ) ppStartPtr, pEndPtr );

            LogDebug( ( "Appending header - content-length=%lu ", networkingLibwebsocketContext.appendHeaders.contentLength ) );
            contentWrittenLength = snprintf( pContentLength, 11, "%lu", networkingLibwebsocketContext.appendHeaders.contentLength );
            status = lws_add_http_header_by_name( wsi, "content-length", pContentLength, contentWrittenLength,
                                                  ( unsigned char ** ) ppStartPtr, pEndPtr );

            /* While fetching credential, append IoT Thing Name. */
            if( networkingLibwebsocketContext.appendHeaders.iotThingNameLength > 0 )
            {
                LogDebug( ( "Appending header - x-amzn-iot-thingname:  %.*s ",( int ) networkingLibwebsocketContext.appendHeaders.iotThingNameLength, networkingLibwebsocketContext.appendHeaders.pIotThingName ) );
                status = lws_add_http_header_by_name( wsi, "x-amzn-iot-thingname", networkingLibwebsocketContext.appendHeaders.pIotThingName, networkingLibwebsocketContext.appendHeaders.iotThingNameLength,
                                                      ( unsigned char ** ) ppStartPtr, pEndPtr );
            }
            else if( networkingLibwebsocketContext.libwebsocketsCredentials.sessionTokenLength > 0 )
            {
                LogDebug( ( "Appending header - x-amz-security-token:  %.*s ",( int ) networkingLibwebsocketContext.libwebsocketsCredentials.sessionTokenLength, networkingLibwebsocketContext.libwebsocketsCredentials.pSessionToken ) );
                status = lws_add_http_header_by_name( wsi, SIGV4_HTTP_X_AMZ_SECURITY_TOKEN_HEADER, networkingLibwebsocketContext.libwebsocketsCredentials.pSessionToken, networkingLibwebsocketContext.libwebsocketsCredentials.sessionTokenLength,
                                                      ( unsigned char ** ) ppStartPtr, pEndPtr );
            }
            else
            {
                /* Empty else marker. */
            }

            lws_client_http_body_pending( wsi, 1 );
            lws_callback_on_writable( wsi );

            break;

        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
            LogDebug( ( "Sending the body %.*s, size %lu ", ( int ) networkingLibwebsocketContext.pRequest->bodyLength, networkingLibwebsocketContext.pRequest->pBody, networkingLibwebsocketContext.pRequest->bodyLength ) );

            writtenBodyLength = lws_write( wsi, networkingLibwebsocketContext.pRequest->pBody, networkingLibwebsocketContext.pRequest->bodyLength, LWS_WRITE_TEXT );

            if( writtenBodyLength != networkingLibwebsocketContext.pRequest->bodyLength )
            {
                LogInfo( ( "Failed to write out the body of POST request entirely. Expected to write %lu, wrote %d ", networkingLibwebsocketContext.pRequest->bodyLength, writtenBodyLength ) );

                if( writtenBodyLength > 0 )
                {
                    // Schedule again
                    lws_client_http_body_pending( wsi, 1 );
                    lws_callback_on_writable( wsi );
                }
                else
                {
                    // Quit
                    retValue = 1;
                }
            }
            else
            {
                lws_client_http_body_pending( wsi, 0 );
            }

            break;

        case LWS_CALLBACK_WSI_DESTROY:
            networkingLibwebsocketContext.terminateLwsService = 1U;
            break;

        default:
            break;
    }

    return retValue;
}

HttpResult_t Http_Init( void * pCredential )
{
    NetworkingLibwebsocketsCredentials_t * pNetworkingLibwebsocketsCredentials = ( NetworkingLibwebsocketsCredentials_t * )pCredential;

    return NetworkingLibwebsockets_Init( pNetworkingLibwebsocketsCredentials );
}

HttpResult_t Http_Send( HttpRequest_t * pRequest,
                        size_t timeoutMs,
                        HttpResponse_t * pResponse )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    NetworkingLibwebsocketHttpVerb_t httpVerb = NETWORKING_LWS_HTTP_VERB_POST;

    memset( &networkingLibwebsocketContext.appendHeaders, 0, sizeof( NetworkingLibwebsocketsAppendHeaders_t ) );

    /* Append HTTP headers for signing.
     * Refer to https://docs.aws.amazon.com/AmazonECR/latest/APIReference/CommonParameters.html for details. */
    /* user-agent */
    networkingLibwebsocketContext.appendHeaders.pUserAgent = networkingLibwebsocketContext.libwebsocketsCredentials.pUserAgent;
    networkingLibwebsocketContext.appendHeaders.userAgentLength = networkingLibwebsocketContext.libwebsocketsCredentials.userAgentLength;

    /* host */
    ret = GetUrlHost( pRequest->pUrl, pRequest->urlLength, &networkingLibwebsocketContext.appendHeaders.pHost, &networkingLibwebsocketContext.appendHeaders.hostLength );

    /* x-amz-date */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = GetIso8601CurrentTime( &networkingLibwebsocketContext.appendHeaders.pDate, &networkingLibwebsocketContext.appendHeaders.dateLength );
    }

    /* content-type - application/json */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        networkingLibwebsocketContext.appendHeaders.pContentType = NETWORKING_LWS_STRING_CONTENT_TYPE_VALUE;
        networkingLibwebsocketContext.appendHeaders.contentTypeLength = strlen( NETWORKING_LWS_STRING_CONTENT_TYPE_VALUE );

        /* content-length - body length */
        if( pRequest->pBody != NULL )
        {
            networkingLibwebsocketContext.appendHeaders.contentLength = pRequest->bodyLength;
        }

        networkingLibwebsocketContext.pRequest = pRequest;
        networkingLibwebsocketContext.pResponse = pResponse;
    }

    /* While fetching credential, append IoT Thing Name and use HTTP GET. */
    if( ( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK ) && ( pRequest->isFetchingCredential != 0U ) )
    {
        httpVerb = NETWORKING_LWS_HTTP_VERB_GET;
        networkingLibwebsocketContext.appendHeaders.pIotThingName = networkingLibwebsocketContext.libwebsocketsCredentials.pIotThingName;
        networkingLibwebsocketContext.appendHeaders.iotThingNameLength = networkingLibwebsocketContext.libwebsocketsCredentials.iotThingNameLength;
    }

    /* Sign this request. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = SignHttpRequest( pRequest );
    }

    /* Blocking execution until getting response from server. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = PerformLwsConnect( networkingLibwebsocketContext.appendHeaders.pHost, networkingLibwebsocketContext.appendHeaders.hostLength, 443U, httpVerb );
    }

    return ret;
}

HttpResult_t Http_UpdateCredential( void * pCredential )
{
    return UpdateCredential( ( NetworkingLibwebsocketsCredentials_t * ) pCredential );
}
