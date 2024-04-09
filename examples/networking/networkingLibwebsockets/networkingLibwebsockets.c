#include <string.h>
#include <time.h>
#include "networkingLibwebsockets.h"
#include "libwebsockets.h"
#include "openssl/sha.h"

#define NETWORKING_LWS_DEFAULT_REGION "us-west-2"
#define NETWORKING_LWS_KVS_SERVICE_NAME "kinesisvideo"
#define NETWORKING_LWS_TIME_LENGTH ( 17 ) /* length of ISO8601 format (e.g. 20111008T070709Z) with NULL terminator */
#define NETWORKING_LWS_STRING_CONTENT_TYPE "content-type"
#define NETWORKING_LWS_STRING_CONTENT_TYPE_VALUE "application/json"
#define NETWORKING_LWS_STRING_SCHEMA_DELIMITER_STRING "://"
#define NETWORKING_LWS_MAX_URI_LENGTH ( 10000 )
#define NETWORKING_LWS_MAX_BUFFER_LENGTH ( LWS_PRE + 2048 ) // General buffer for libwebsockets to send/read data.
#define NETWORKING_LWS_USER_AGENT_NAME_MAX_LENGTH ( 128 )

static int32_t sha256Init( void * hashContext );
static int32_t sha256Update( void * hashContext,
                             const uint8_t * pInput,
                             size_t inputLen );
static int32_t sha256Final( void * hashContext,
                            uint8_t * pOutput,
                            size_t outputLen );

char pLwsBuffer[ NETWORKING_LWS_MAX_BUFFER_LENGTH ];
char pathBuffer[ NETWORKING_LWS_MAX_URI_LENGTH + 1 ];
uint32_t pathBufferWrittenLength = 0;
char sigv4AuthBuffer[ 2048 ];
size_t sigv4AuthLen = 2048;

static struct lws_protocols protocols[2];
/**
 * @brief CryptoInterface provided to SigV4 library for generating the hash digest.
 */
static SigV4CryptoInterface_t cryptoInterface =
{
    .hashInit      = sha256Init,
    .hashUpdate    = sha256Update,
    .hashFinal     = sha256Final,
    .pHashContext  = NULL,
    .hashBlockLen  = 64,
    .hashDigestLen = 32,
};

static SigV4Parameters_t sigv4Params =
{
    .pCredentials     = NULL,
    .pDateIso8601     = NULL,
    .pRegion          = NETWORKING_LWS_DEFAULT_REGION,
    .regionLen        = sizeof( NETWORKING_LWS_DEFAULT_REGION ) - 1,
    .pService         = NETWORKING_LWS_KVS_SERVICE_NAME,
    .serviceLen       = sizeof( NETWORKING_LWS_KVS_SERVICE_NAME ) - 1,
    .pCryptoInterface = &cryptoInterface,
    .pHttpParameters  = NULL
};

static int32_t sha256Init( void * hashContext )
{
    SHA256_Init( hashContext );
    return 0;
}

static int32_t sha256Update( void * hashContext,
                             const uint8_t * pInput,
                             size_t inputLen )
{
    int32_t ret = 0;
    ret = SHA256_Update( hashContext, pInput, inputLen );
    return ret == 1? 0:-1;
}

static int32_t sha256Final( void * hashContext,
                            uint8_t * pOutput,
                            size_t outputLen )
{
    int32_t ret = 0;

    ( void ) outputLen;

    ret = SHA256_Final( pOutput, hashContext );

    return ret == 1? 0:-1;
}

static HttpResult_t generateAuthorizationHeader( NetworkingLibwebsocketContext_t *pCtx )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    SigV4HttpParameters_t sigv4HttpParams;
    SigV4Status_t sigv4Status = SigV4Success;
    /* Store Signature used in AWS HTTP requests generated using SigV4 library. */
    char * signature = NULL;
    size_t signatureLen = 0;
    char headersBuffer[ 4096 ];
    size_t remainSize = sizeof( headersBuffer );
    int32_t snprintfReturn;
    NetworkingLibwebsocketsAppendHeaders_t *pAppendHeaders = &pCtx->appendHeaders;
    char *pHostEnd;

    /* Calculate remaining length of URL after host. */
    pHostEnd = pCtx->appendHeaders.pHost + pCtx->appendHeaders.hostLength;
    pathBufferWrittenLength = pCtx->pRequest->urlLength - ( pHostEnd - pCtx->pRequest->pUrl );
    memcpy( pathBuffer, pHostEnd, pathBufferWrittenLength );
    pathBuffer[pathBufferWrittenLength] = '\0';

    /* path must start with '/' */
    pathBuffer[0] = '/';

    /* Prepare headers for Authorization.
     * Format: "user-agent: AWS-SDK-KVS\r\nhost: kinesisvideo.us-west-2.amazonaws.com\r\n..." */
    snprintfReturn = snprintf( headersBuffer, remainSize, "%s: %.*s\r\n%s: %.*s\r\n%s: %.*s\r\n",
                               "host", ( int ) pAppendHeaders->hostLength, pAppendHeaders->pHost,
                               "user-agent", ( int ) pAppendHeaders->userAgentLength, pAppendHeaders->pUserAgent,
                               "x-amz-date", ( int ) pAppendHeaders->dateLength, pAppendHeaders->pDate );

    if( snprintfReturn < 0 )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
    }
    else if( snprintfReturn == remainSize )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_AUTH_BUFFER_TOO_SMALL;
    }
    else
    {
        /* Do nothing, Coverity happy. */
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        /* Setup the HTTP parameters. */
        sigv4HttpParams.pHttpMethod = "POST";
        sigv4HttpParams.httpMethodLen = strlen("POST");
        /* None of the requests parameters below are pre-canonicalized */
        sigv4HttpParams.flags = 0;
        sigv4HttpParams.pPath = pathBuffer;
        sigv4HttpParams.pathLen = pathBufferWrittenLength;
        /* AWS S3 request does not require any Query parameters. */
        sigv4HttpParams.pQuery = NULL;
        sigv4HttpParams.queryLen = 0;
        sigv4HttpParams.pHeaders = headersBuffer;
        sigv4HttpParams.headersLen = snprintfReturn;
        sigv4HttpParams.pPayload = pCtx->pRequest->pBody;
        sigv4HttpParams.payloadLen = pCtx->pRequest->bodyLength;
        
        /* Initializing sigv4Params with Http parameters required for the HTTP request. */
        sigv4Params.pRegion = pCtx->libwebsocketsCredentials.pRegion;
        sigv4Params.regionLen = pCtx->libwebsocketsCredentials.regionLength;
        sigv4Params.pHttpParameters = &sigv4HttpParams;
        sigv4Params.pCredentials = &pCtx->sigv4Credential;
        sigv4Params.pDateIso8601 = pCtx->appendHeaders.pDate;
        cryptoInterface.pHashContext = &pCtx->sha256Ctx;

        /* Reset buffer length */
        sigv4AuthLen = 2048;
        sigv4Status = SigV4_GenerateHTTPAuthorization( &sigv4Params, sigv4AuthBuffer, &sigv4AuthLen, &signature, &signatureLen );
        
        if( sigv4Status != SigV4Success )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SIGV4_GENERATE_AUTH_FAIL;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        pCtx->appendHeaders.pAuthorization = sigv4AuthBuffer;
        pCtx->appendHeaders.authorizationLength = sigv4AuthLen;
    }

    return ret;
}

static HttpResult_t getIso8601CurrentTime( char **ppDate, size_t * pDateLength )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    static char iso8601TimeBuf[NETWORKING_LWS_TIME_LENGTH] = { 0 };
    time_t now;
    size_t timeLength = 0;

    if( ppDate == NULL || pDateLength == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        time(&now);
        timeLength = strftime(iso8601TimeBuf, NETWORKING_LWS_TIME_LENGTH, "%Y%m%dT%H%M%SZ", gmtime(&now));

        if( timeLength <= 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_TIME_BUFFER_TOO_SMALL;
        }
    }
    
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        *ppDate = iso8601TimeBuf;
        *pDateLength = timeLength;
    }

    return ret;
}

static HttpResult_t getUrlHost( char *pUrl, size_t urlLength, char **ppStart, size_t *pHostLength )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char *pStart = NULL, *pEnd = pUrl + urlLength, *pCurPtr;
    uint8_t foundEndMark = 0;

    if( pUrl == NULL || ppStart == NULL || pHostLength == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        // Start from the schema delimiter
        pStart = strstr(pUrl, NETWORKING_LWS_STRING_SCHEMA_DELIMITER_STRING);
        if( pStart == NULL )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SCHEMA_DELIMITER_NOT_FOUND;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        // Advance the pStart past the delimiter
        pStart += strlen(NETWORKING_LWS_STRING_SCHEMA_DELIMITER_STRING);

        if( pStart > pEnd )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_EXCEED_URL_LENGTH;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        // Find the delimiter which would indicate end of the host - either one of "/:?"
        pCurPtr = pStart;

        while( !foundEndMark && pCurPtr <= pEnd )
        {
            switch( *pCurPtr )
            {
                case '/':
                case ':':
                case '?':
                    foundEndMark = 1;
                    break;
                default:
                    pCurPtr++;
            }
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        *ppStart = pStart;
        *pHostLength = pCurPtr - pStart;
    }

    return ret;
}

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
    connectInfo.path = pathBuffer;
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

static int32_t lwsHttpCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize)
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

                if( lws_http_client_read(wsi, (char**)&pLwsBuffer, &readLength) < 0 )
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
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    struct lws_context_creation_info creationInfo;
    const lws_retry_bo_t retryPolicy = {
        .secs_since_valid_ping = 10,
        .secs_since_valid_hangup = 7200,
    };
    NetworkingLibwebsocketContext_t *pCtx = (NetworkingLibwebsocketContext_t *) pHttpCtx;
    NetworkingLibwebsocketsCredentials_t *pNetworkingLibwebsocketsCredentials = (NetworkingLibwebsocketsCredentials_t *)pCredential;

    if( pCtx == NULL || pCredential == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        memcpy( &pCtx->libwebsocketsCredentials, pNetworkingLibwebsocketsCredentials, sizeof(NetworkingLibwebsocketsCredentials_t) );
        pCtx->sigv4Credential.pAccessKeyId = pNetworkingLibwebsocketsCredentials->pAccessKeyId;
        pCtx->sigv4Credential.accessKeyIdLen = pNetworkingLibwebsocketsCredentials->accessKeyIdLength;
        pCtx->sigv4Credential.pSecretAccessKey = pNetworkingLibwebsocketsCredentials->pSecretAccessKey;
        pCtx->sigv4Credential.secretAccessKeyLen = pNetworkingLibwebsocketsCredentials->secretAccessKeyLength;

        if( pCtx->libwebsocketsCredentials.userAgentLength > NETWORKING_LWS_USER_AGENT_NAME_MAX_LENGTH )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_USER_AGENT_NAME_TOO_LONG;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        protocols[0].name = "https";
        protocols[0].callback = lwsHttpCallbackRoutine;
        // protocols[1].name = "wss";
        // protocols[1].callback = lwsCallback;

        memset(&creationInfo, 0, sizeof(struct lws_context_creation_info));
        creationInfo.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        creationInfo.port = CONTEXT_PORT_NO_LISTEN;
        creationInfo.protocols = protocols;
        creationInfo.timeout_secs = 10;
        creationInfo.gid = -1;
        creationInfo.uid = -1;
        creationInfo.client_ssl_ca_filepath = pCtx->libwebsocketsCredentials.pCaCertPath;
        creationInfo.client_ssl_cipher_list = "HIGH:!PSK:!RSP:!eNULL:!aNULL:!RC4:!MD5:!DES:!3DES:!aDH:!kDH:!DSS";
        creationInfo.ka_time = 1;
        creationInfo.ka_probes = 1;
        creationInfo.ka_interval = 1;
        creationInfo.retry_and_idle_policy = &retryPolicy;

        lws_set_log_level(LLL_NOTICE | LLL_WARN | LLL_ERR, NULL);
        pCtx->pLwsContext = lws_create_context( &creationInfo );

        if( pCtx->pLwsContext == NULL )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_INIT_LWS_CONTEXT_FAIL;
        }
    }

    return ret;
}

HttpResult_t Http_Send( HttpContext_t *pHttpCtx, HttpRequest_t *pRequest, size_t timeoutMs, HttpResponse_t *pResponse )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    struct lws_client_connect_info connectInfo;
    struct lws *clientLws;
    NetworkingLibwebsocketContext_t *pCtx = (NetworkingLibwebsocketContext_t *) pHttpCtx;

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

    /* Authorization */
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
