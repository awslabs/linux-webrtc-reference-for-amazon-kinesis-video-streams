#include "networkingLibwebsockets.h"
#include "networkingLibwebsockets_private.h"
#include "libwebsockets.h"

#define NETWORKING_LWS_STRING_CONTENT_TYPE "content-type"
#define NETWORKING_LWS_STRING_CONTENT_TYPE_VALUE "application/json"

static HttpResult_t performHttpRequest( NetworkingLibwebsocketContext_t *pCtx )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    struct lws_client_connect_info connectInfo;
    struct lws* clientLws;
    int32_t lwsReturn;
    static char pHost[31];

    memcpy( pHost, pCtx->appendHeaders.pHost, pCtx->appendHeaders.hostLength );
    pHost[pCtx->appendHeaders.hostLength] = '\0';

    memset( &connectInfo, 0, sizeof( struct lws_client_connect_info ) );
    connectInfo.context = pCtx->pLwsContext;
    connectInfo.ssl_connection = LCCSCF_USE_SSL;
    connectInfo.port = 443;
    connectInfo.address = pHost;
    connectInfo.path = pCtx->pathBuffer;
    connectInfo.host = connectInfo.address;
    connectInfo.method = "POST";
    connectInfo.protocol = "https";
    connectInfo.pwsi = &clientLws;

    connectInfo.opaque_user_data = pCtx;

    pCtx->pLws = lws_client_connect_via_info( &connectInfo );

    pCtx->terminateLwsService = 0U;
    while( pCtx->terminateLwsService == 0U )
    {
        lwsReturn = lws_service(pCtx->pLwsContext, 0);
    }

    return ret;
}

int32_t lwsHttpCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize)
{
    int32_t retValue = 0;
    int32_t status, writtenBodyLength, readLength;
    void *pCustomData;
    char *pCurPtr;
    // char dateHdrBuffer[MAX_DATE_HEADER_BUFFER_LENGTH + 1];
    char *pEndPtr;
    char **ppStartPtr;
    uint64_t item, serverTime;
    uint32_t headerCount;
    uint32_t logLevel;
    time_t td;
    size_t len;
    uint64_t nowTime, clockSkew = 0;
    NetworkingLibwebsocketContext_t *pCtx;

    char pContentLength[ 11 ]; /* It needs 10 bytes for 32 bit integer, +1 for NULL terminator. */
    size_t contentWrittenLength;

    ( void ) pUser;

    printf( "HTTP callback with reason %d\n", reason );

    // Early check before accessing the custom data field to see if we are interested in processing the message
    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
            break;
        default:
            retValue = 0;
    }

    if( retValue == 0 )
    {
        pCtx = (NetworkingLibwebsocketContext_t *)lws_get_opaque_user_data(wsi);
    }

    if( retValue == 0 )
    {
        switch (reason) {
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
                status = lws_http_client_http_response(wsi);
                // getStateMachineCurrentState(pSignalingClient->pStateMachine, &pStateMachineState);

                printf( "Connected with server response: %d\n", status );
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
                printf( "Received client http read: %lu bytes\n", dataSize );
                lwsl_hexdump_debug(pDataIn, dataSize);

                if( dataSize != 0 )
                {
                    if( dataSize >= pCtx->pResponse->bufferLength )
                    {
                        /* Receive data is larger than buffer size. */
                        retValue = -2;
                    }
                    else
                    {
                        memcpy( pCtx->pResponse->pBuffer, pDataIn, dataSize );
                        pCtx->pResponse->pBuffer[dataSize] = '\0';
                        pCtx->pResponse->bufferLength = dataSize;
                        
                        printf( "%s\n", pCtx->pResponse->pBuffer );
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
                printf( "Received client http\n" );
                readLength = NETWORKING_LWS_MAX_BUFFER_LENGTH;

                if( lws_http_client_read(wsi, (char**)pCtx->pLwsBuffer, &readLength) < 0 )
                {
                    retValue = -1;
                }

                break;

            case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
                // DLOGD("Http client completed");
                break;

            case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
                printf( "Client append handshake header\n" );

                ppStartPtr = (char**) pDataIn;
                pEndPtr = *ppStartPtr + dataSize - 1;

                // Append headers
                printf( "Appending header - Authorization=%.*s\n", ( int ) pCtx->appendHeaders.authorizationLength, pCtx->appendHeaders.pAuthorization );
                status = lws_add_http_header_by_name( wsi, "Authorization", pCtx->appendHeaders.pAuthorization, pCtx->appendHeaders.authorizationLength,
                                                      ( unsigned char ** ) ppStartPtr, pEndPtr );

                /* user-agent */
                printf( "Appending header - user-agent=%.*s\n", ( int ) pCtx->appendHeaders.userAgentLength, pCtx->appendHeaders.pUserAgent );
                status = lws_add_http_header_by_name( wsi, "user-agent", pCtx->appendHeaders.pUserAgent, pCtx->appendHeaders.userAgentLength,
                                                      ( unsigned char ** ) ppStartPtr, pEndPtr );

                printf( "Appending header - x-amz-date=%.*s\n", ( int ) pCtx->appendHeaders.dateLength, pCtx->appendHeaders.pDate );
                status = lws_add_http_header_by_name( wsi, "x-amz-date", pCtx->appendHeaders.pDate, pCtx->appendHeaders.dateLength,
                                                      ( unsigned char ** ) ppStartPtr, pEndPtr );

                printf( "Appending header - content-type=%.*s\n", ( int ) pCtx->appendHeaders.contentTypeLength, pCtx->appendHeaders.pContentType );
                status = lws_add_http_header_by_name( wsi, "content-type", pCtx->appendHeaders.pContentType, pCtx->appendHeaders.contentTypeLength,
                                                      ( unsigned char ** ) ppStartPtr, pEndPtr );

                printf( "Appending header - content-length=%lu\n", pCtx->appendHeaders.contentLength );
                contentWrittenLength = snprintf( pContentLength, 11, "%lu", pCtx->appendHeaders.contentLength );
                status = lws_add_http_header_by_name( wsi, "content-length", pContentLength, contentWrittenLength,
                                                      ( unsigned char ** ) ppStartPtr, pEndPtr );

                lws_client_http_body_pending(wsi, 1);
                lws_callback_on_writable(wsi);

                break;

            case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
                printf( "Sending the body %.*s, size %lu\n", ( int ) pCtx->pRequest->bodyLength, pCtx->pRequest->pBody, pCtx->pRequest->bodyLength );

                writtenBodyLength = lws_write(wsi, pCtx->pRequest->pBody, pCtx->pRequest->bodyLength, LWS_WRITE_TEXT);

                if( writtenBodyLength != pCtx->pRequest->bodyLength )
                {
                    printf( "Failed to write out the body of POST request entirely. Expected to write %lu, wrote %d\n", pCtx->pRequest->bodyLength, writtenBodyLength );

                    if( writtenBodyLength > 0 )
                    {
                        // Schedule again
                        lws_client_http_body_pending(wsi, 1);
                        lws_callback_on_writable(wsi);
                    }
                    else
                    {
                        // Quit
                        retValue = 1;
                    }
                }
                else
                {
                    lws_client_http_body_pending(wsi, 0);
                }

                break;

            case LWS_CALLBACK_WSI_DESTROY:
                pCtx->terminateLwsService = 1U;
                break;

            default:
                break;
        }
    }

    // if (STATUS_FAILED(retStatus)) {
    //     lws_cancel_service(lws_get_context(wsi));
    // }

    return retValue;
}

HttpResult_t Http_Init( HttpContext_t *pHttpCtx, void * pCredential )
{
    NetworkingLibwebsocketContext_t *pCtx = (NetworkingLibwebsocketContext_t *) pHttpCtx;
    NetworkingLibwebsocketsCredentials_t *pNetworkingLibwebsocketsCredentials = (NetworkingLibwebsocketsCredentials_t *)pCredential;

    return NetworkingLibwebsockets_Init( pCtx, pNetworkingLibwebsocketsCredentials );
}

HttpResult_t Http_Send( HttpContext_t *pHttpCtx, HttpRequest_t *pRequest, size_t timeoutMs, HttpResponse_t *pResponse )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    struct lws_client_connect_info connectInfo;
    struct lws *clientLws;
    NetworkingLibwebsocketContext_t *pCtx = (NetworkingLibwebsocketContext_t *) pHttpCtx;
    char *pPath;
    size_t pathLength;

    /* Append HTTP headers for signing.
     * Refer to https://docs.aws.amazon.com/AmazonECR/latest/APIReference/CommonParameters.html for details. */
    /* user-agent */
    pCtx->appendHeaders.pUserAgent = pCtx->libwebsocketsCredentials.pUserAgent;
    pCtx->appendHeaders.userAgentLength = pCtx->libwebsocketsCredentials.userAgentLength;

    /* host */
    ret = getUrlHost( pRequest->pUrl, pRequest->urlLength, &pCtx->appendHeaders.pHost, &pCtx->appendHeaders.hostLength );

    /* x-amz-date */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = getIso8601CurrentTime( &pCtx->appendHeaders.pDate, &pCtx->appendHeaders.dateLength );
    }
    
    /* content-type - application/json */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        pCtx->appendHeaders.pContentType = NETWORKING_LWS_STRING_CONTENT_TYPE_VALUE;
        pCtx->appendHeaders.contentTypeLength = strlen( NETWORKING_LWS_STRING_CONTENT_TYPE_VALUE );

        /* content-length - body length */
        if( pRequest->pBody != NULL )
        {
            pCtx->appendHeaders.contentLength = pRequest->bodyLength;
        }

        pCtx->pRequest = pRequest;
        pCtx->pResponse = pResponse;
    }

    /* Calculate authorization headers */
    /* Find path from URL first. Then use the path to generate authorization headers. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = getPathFromUrl( pRequest->pUrl, pRequest->urlLength, &pPath, &pathLength );
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        if( pathLength > NETWORKING_LWS_MAX_URI_LENGTH )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_PATH_BUFFER_TOO_SMALL;
        }
        else
        {
            memcpy( pCtx->pathBuffer, pPath, pathLength );
            pCtx->pathBuffer[ pathLength ] = '\0';
            pCtx->pathBuffer[ 0 ] = '/';
            pCtx->pathBufferWrittenLength = pathLength;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = generateAuthorizationHeader( pCtx );
    }

    /* Blocking execution until getting response from server. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = performHttpRequest( pCtx );
    }

    return ret;
}