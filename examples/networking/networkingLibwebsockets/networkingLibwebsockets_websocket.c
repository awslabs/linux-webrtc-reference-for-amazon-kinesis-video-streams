#include "logging.h"
#include "networkingLibwebsockets.h"
#include "networkingLibwebsockets_private.h"

#define NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME "X-Amz-ChannelARN"
#define NETWORKING_LWS_STRING_CREDENTIAL_PARAM_NAME "X-Amz-Credential"
#define NETWORKING_LWS_STRING_DATE_PARAM_NAME "X-Amz-Date"
#define NETWORKING_LWS_STRING_EXPIRES_PARAM_NAME "X-Amz-Expires"
#define NETWORKING_LWS_STRING_SIGNED_HEADERS_PARAM_NAME "X-Amz-SignedHeaders"
#define NETWORKING_LWS_STRING_SIGNATURE_PARAM_NAME "X-Amz-Signature"
#define NETWORKING_LWS_STRING_SIGNED_HEADERS_VALUE "host"

#define NETWORKING_LWS_STRING_CREDENTIAL_VALUE_TEMPLATE "%.*s/%.*s/%.*s/" NETWORKING_LWS_KVS_SERVICE_NAME "/aws4_request"

#define NETWORKING_LWS_CREDENTIAL_PARAM_DATE_LENGTH ( 8 )
#define NETWORKING_LWS_STATIC_CRED_EXPIRES_SECONDS ( 604800 )

static NetworkingLibwebsocketsResult_t writeUriEncodedAlgorithm( char **ppBuffer, size_t *pBufferLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t writtenLength;

    writtenLength = snprintf( *ppBuffer, *pBufferLength, "X-Amz-Algorithm=AWS4-HMAC-SHA256" );

    if( writtenLength < 0 )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
    }
    else if( writtenLength == *pBufferLength )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
    }
    else
    {
        *ppBuffer += writtenLength;
        *pBufferLength -= writtenLength;
    }

    return ret;
}

static NetworkingLibwebsocketsResult_t writeUriEncodedChannelArn( char **ppBuffer, size_t *pBufferLength, char *pChannelArn, size_t channelArnLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t writtenLength;
    size_t encodedLength;

    /* X-Amz-ChannelARN query parameter. */
    writtenLength = snprintf( *ppBuffer, *pBufferLength, "&X-Amz-ChannelARN=" );

    if( writtenLength < 0 )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
    }
    else if( writtenLength == *pBufferLength )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
    }
    else
    {
        *ppBuffer += writtenLength;
        *pBufferLength -= writtenLength;
    }

    /* X-Amz-ChannelARN value (plaintext). */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        writtenLength = snprintf( *ppBuffer, *pBufferLength, "%.*s",
                                  ( int ) channelArnLength, pChannelArn );

        if( writtenLength < 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
        }
        else if( writtenLength == *pBufferLength )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
        }
        else
        {
            /* Keep the pointer and *pBufferLength for URI encoded. */
        }
    }

    /* X-Amz-ChannelARN value (URI encoded). */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        encodedLength = *pBufferLength - writtenLength;
        ret = uriEncodedString( *ppBuffer, writtenLength, (*ppBuffer) + writtenLength, &encodedLength );

        /* Move and update pointer/remain length. */
        if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
        {
            memmove( *ppBuffer, *ppBuffer + writtenLength, encodedLength );
            *ppBuffer += encodedLength;
            *pBufferLength -= encodedLength;
        }
    }

    return ret;
}

static NetworkingLibwebsocketsResult_t writeUriEncodedCredential( char **ppBuffer, size_t *pBufferLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t writtenLength;
    size_t encodedLength;

    /* X-Amz-Credential query parameter. */
    writtenLength = snprintf( *ppBuffer, *pBufferLength, "&" NETWORKING_LWS_STRING_CREDENTIAL_PARAM_NAME "=" );

    if( writtenLength < 0 )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
    }
    else if( writtenLength == *pBufferLength )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
    }
    else
    {
        *ppBuffer += writtenLength;
        *pBufferLength -= writtenLength;
    }

    /* X-Amz-Credential value (plaintext). */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        writtenLength = snprintf( *ppBuffer, *pBufferLength, NETWORKING_LWS_STRING_CREDENTIAL_VALUE_TEMPLATE,
                                  ( int ) networkingLibwebsocketContext.libwebsocketsCredentials.accessKeyIdLength, networkingLibwebsocketContext.libwebsocketsCredentials.pAccessKeyId,
                                  NETWORKING_LWS_CREDENTIAL_PARAM_DATE_LENGTH, networkingLibwebsocketContext.appendHeaders.pDate,
                                  ( int ) networkingLibwebsocketContext.libwebsocketsCredentials.regionLength, networkingLibwebsocketContext.libwebsocketsCredentials.pRegion );

        if( writtenLength < 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
        }
        else if( writtenLength == *pBufferLength )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
        }
        else
        {
            /* Keep the pointer and pBufferLength for URI encoded. */
        }
    }

    /* X-Amz-Credential value (URI encoded) */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        encodedLength = *pBufferLength - writtenLength;
        ret = uriEncodedString( *ppBuffer, writtenLength, *ppBuffer + writtenLength, &encodedLength );

        /* Move and update pointer/remain length. */
        if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
        {
            memmove( *ppBuffer, *ppBuffer + writtenLength, encodedLength );
            *ppBuffer += encodedLength;
            *pBufferLength -= encodedLength;
        }
    }

    return ret;
}

static NetworkingLibwebsocketsResult_t writeUriEncodedDate( char **ppBuffer, size_t *pBufferLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t writtenLength;
    size_t encodedLength;

    /* X-Amz-Date query parameter. */
    writtenLength = snprintf( *ppBuffer, *pBufferLength, "&" NETWORKING_LWS_STRING_DATE_PARAM_NAME "=" );

    if( writtenLength < 0 )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
    }
    else if( writtenLength == *pBufferLength )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
    }
    else
    {
        *ppBuffer += writtenLength;
        *pBufferLength -= writtenLength;
    }

    /* X-Amz-Date value (plaintext). */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        writtenLength = snprintf( *ppBuffer, *pBufferLength, "%.*s",
                                  ( int ) networkingLibwebsocketContext.appendHeaders.dateLength, networkingLibwebsocketContext.appendHeaders.pDate );

        if( writtenLength < 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
        }
        else if( writtenLength == *pBufferLength )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
        }
        else
        {
            /* Keep the pointer and pBufferLength for URI encoded. */
        }
    }

    /* X-Amz-Date value (URI encoded) */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        encodedLength = *pBufferLength - writtenLength;
        ret = uriEncodedString( *ppBuffer, writtenLength, *ppBuffer + writtenLength, &encodedLength );

        /* Move and update pointer/remain length. */
        if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
        {
            memmove( *ppBuffer, *ppBuffer + writtenLength, encodedLength );
            *ppBuffer += encodedLength;
            *pBufferLength -= encodedLength;
        }
    }

    return ret;
}

static NetworkingLibwebsocketsResult_t writeUriEncodedExpires( char **ppBuffer, size_t *pBufferLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t writtenLength;
    size_t encodedLength;

    /* X-Amz-Expires query parameter. */
    writtenLength = snprintf( *ppBuffer, *pBufferLength, "&" NETWORKING_LWS_STRING_EXPIRES_PARAM_NAME "=" );

    if( writtenLength < 0 )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
    }
    else if( writtenLength == *pBufferLength )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
    }
    else
    {
        *ppBuffer += writtenLength;
        *pBufferLength -= writtenLength;
    }

    /* X-Amz-Expires value (plaintext). */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        writtenLength = snprintf( *ppBuffer, *pBufferLength, "%d", NETWORKING_LWS_STATIC_CRED_EXPIRES_SECONDS );

        if( writtenLength < 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
        }
        else if( writtenLength == *pBufferLength )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
        }
        else
        {
            /* Keep the pointer and pBufferLength for URI encoded. */
        }
    }

    /* X-Amz-Expires value (URI encoded) */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        encodedLength = *pBufferLength - writtenLength;
        ret = uriEncodedString( *ppBuffer, writtenLength, *ppBuffer + writtenLength, &encodedLength );

        /* Move and update pointer/remain length. */
        if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
        {
            memmove( *ppBuffer, *ppBuffer + writtenLength, encodedLength );
            *ppBuffer += encodedLength;
            *pBufferLength -= encodedLength;
        }
    }

    return ret;
}

static NetworkingLibwebsocketsResult_t writeUriEncodedSignedHeaders( char **ppBuffer, size_t *pBufferLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t writtenLength;

    /* X-Amz-SignedHeaders query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        writtenLength = snprintf( *ppBuffer, *pBufferLength, "&" NETWORKING_LWS_STRING_SIGNED_HEADERS_PARAM_NAME "=" NETWORKING_LWS_STRING_SIGNED_HEADERS_VALUE );

        if( writtenLength < 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
        }
        else if( writtenLength == *pBufferLength )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
        }
        else
        {
            *ppBuffer += writtenLength;
            *pBufferLength -= writtenLength;
        }
    }

    return ret;
}

static NetworkingLibwebsocketsResult_t generateQueryParameters( char *pQueryStart, size_t queryLength, char *pOutput, size_t *pOutputLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char *pChannelArnQueryParam, *pChannelArnValue, *pEqual;
    size_t channelArnQueryParamLength, channelArnValueLength;
    char *pCurrentWrite = pOutput;
    size_t remainLength;

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

    /* Append X-Amz-Algorithm, X-Amz-ChannelARN, X-Amz-Credential, X-Amz-Date, X-Amz-Expires, and X-Amz-SignedHeaders first
     * to generate signature. Then append X-Amz-Signature after getting it from sigv4 API.
     *
     * Note that the order of query parameters is important. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        remainLength = *pOutputLength;

        ret = writeUriEncodedAlgorithm( &pCurrentWrite, &remainLength );
    }

    /* X-Amz-ChannelARN query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = writeUriEncodedChannelArn( &pCurrentWrite, &remainLength, pChannelArnValue, channelArnValueLength );
    }

    /* X-Amz-Credential query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = writeUriEncodedCredential( &pCurrentWrite, &remainLength );
    }

    /* X-Amz-Date query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = writeUriEncodedDate( &pCurrentWrite, &remainLength );
    }

    /* X-Amz-Expires query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = writeUriEncodedExpires( &pCurrentWrite, &remainLength );
    }

    /* X-Amz-SignedHeaders query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = writeUriEncodedSignedHeaders( &pCurrentWrite, &remainLength );
    }
    
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        *pOutputLength = *pOutputLength - remainLength;
    }

    return ret;
}

static NetworkingLibwebsocketsResult_t signWebsocketRequest( WebsocketServerInfo_t *pWebsocketServerInfo )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    int32_t headerLength;
    NetworkingLibwebsocketsAppendHeaders_t *pAppendHeaders = &networkingLibwebsocketContext.appendHeaders;
    NetworkingLibwebsocketCanonicalRequest_t canonicalRequest;
    char *pPath, *pQueryStart, *pUrlEnd;
    size_t pathLength, queryLength, remainLength;
    size_t queryParamsStringLength;
    
    /* Find the path for request. */
    ret = getPathFromUrl( pWebsocketServerInfo->pUrl, pWebsocketServerInfo->urlLength, &pPath, &pathLength );

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        pUrlEnd = pWebsocketServerInfo->pUrl + pWebsocketServerInfo->urlLength;
        pQueryStart = pPath + pathLength + 1; // +1 to skip '?' mark.
        queryLength = pUrlEnd - pQueryStart;

        if( queryLength <= 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_UNEXPECTED_WEBSOCKET_URL;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        /* Follow https://docs.aws.amazon.com/IAM/latest/UserGuide/create-signed-request.html to create query parameters. */
        queryParamsStringLength = NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH;
        ret = generateQueryParameters( pQueryStart,
                                             queryLength,
                                             networkingLibwebsocketContext.sigv4Metadatabuffer,
                                             &queryParamsStringLength );
    }

    /* Follow https://docs.aws.amazon.com/IAM/latest/UserGuide/create-signed-request.html to create canonical headers.
     * Websocket Format: "host: kinesisvideo.us-west-2.amazonaws.com\r\n"
     *
     * Note that we re-use the parsed result in pAppendHeaders from Websocket_Connect(). */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        remainLength = NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH - queryParamsStringLength;

        headerLength = snprintf( networkingLibwebsocketContext.sigv4Metadatabuffer + queryParamsStringLength, remainLength, "%s: %.*s\r\n",
                                 "host", ( int ) pAppendHeaders->hostLength, pAppendHeaders->pHost );

        if( headerLength < 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
        }
        else if( headerLength == remainLength )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_AUTH_BUFFER_TOO_SMALL;
        }
        else
        {
            /* Do nothing, Coverity happy. */
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        memset( &canonicalRequest, 0, sizeof( canonicalRequest ) );
        canonicalRequest.verb = NETWORKING_LWS_HTTP_VERB_GET;
        canonicalRequest.pPath = pPath;
        canonicalRequest.pathLength = pathLength;
        canonicalRequest.pCanonicalQueryString = networkingLibwebsocketContext.sigv4Metadatabuffer;
        canonicalRequest.canonicalQueryStringLength = queryParamsStringLength;
        canonicalRequest.pCanonicalHeaders = networkingLibwebsocketContext.sigv4Metadatabuffer + queryParamsStringLength;
        canonicalRequest.canonicalHeadersLength = headerLength;
        canonicalRequest.pPayload = NULL;
        canonicalRequest.payloadLength = 0U;

        ret = generateAuthorizationHeader( &canonicalRequest );
    }

    /* Append signature. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        remainLength = NETWORKING_LWS_SIGV4_METADATA_BUFFER_LENGTH - queryParamsStringLength;

        headerLength = snprintf( networkingLibwebsocketContext.sigv4Metadatabuffer + queryParamsStringLength, remainLength, "&" NETWORKING_LWS_STRING_SIGNATURE_PARAM_NAME "=%.*s",
                                 ( int ) pAppendHeaders->signatureLength, pAppendHeaders->pSignature );

        if( headerLength < 0 )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SNPRINTF_FAIL;
        }
        else if( headerLength == remainLength )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_QUERY_PARAM_BUFFER_TOO_SMALL;
        }
        else
        {
            /* Do nothing, coverity happy. */
        }
    }

    /* Store query parameters into path buffer. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        pathLength = 1U + 1U + queryParamsStringLength + headerLength + 1U; // "/?" + URI encoded query parameters + NULL terminator ("/?X-Amz-Algorithm=AWS4-HMAC-SHA256&...")
        if( pathLength >= NETWORKING_LWS_MAX_URI_LENGTH )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_PATH_BUFFER_TOO_SMALL;
        }
        else
        {
            networkingLibwebsocketContext.pathBuffer[ 0 ] = '/';
            networkingLibwebsocketContext.pathBuffer[ 1 ] = '?';
            memcpy( &networkingLibwebsocketContext.pathBuffer[ 2 ], networkingLibwebsocketContext.sigv4Metadatabuffer, queryParamsStringLength + headerLength );
            networkingLibwebsocketContext.pathBuffer[ pathLength - 1 ] = '\0'; // Append NULL terminator for libwebsockets.
            networkingLibwebsocketContext.pathBufferWrittenLength = pathLength - 1;
        }
    }

    return ret;
}

int32_t lwsWebsocketCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *pUser, void *pDataIn, size_t dataSize)
{
    int32_t retValue = 0;
    int32_t skipProcess = 0;
    NetworkingLibwebsocketsResult_t websocketRet = NETWORKING_LIBWEBSOCKETS_RESULT_OK;

    LogInfo( ( "Websocket callback with reason %d", reason ) );
    
    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            LogWarn( ( "Client WSS connection error" ) );
            networkingLibwebsocketContext.terminateLwsService = 1U;
            break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            LogInfo( ( "Connection established" ) );
            networkingLibwebsocketContext.terminateLwsService = 1U;
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            LogInfo( ( "Client WSS closed" ) );
            networkingLibwebsocketContext.terminateLwsService = 1U;
            break;

        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
            LogInfo( ( "Peer closed the connection" ) );
            networkingLibwebsocketContext.terminateLwsService = 1U;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            if( lws_frame_is_binary(wsi) ||
                dataSize == 0 )
            {
                /* Binary data is not supported. */
                skipProcess = 1;
            }

            if( skipProcess == 0 )
            {
                /* Store the message into buffer. */
                // Check what type of a message it is. We will set the size to 0 on first and flush on last
                if( lws_is_first_fragment(wsi) )
                {
                    networkingLibwebsocketContext.websocketRxBufferLength = 0;
                }

                // Store the data in the buffer
                if( networkingLibwebsocketContext.websocketRxBufferLength + dataSize < NETWORKING_LWS_WEBSOCKET_RX_BUFFER_LENGTH )
                {
                    memcpy( &networkingLibwebsocketContext.websocketRxBuffer[ networkingLibwebsocketContext.websocketRxBufferLength ], pDataIn, dataSize );
                    networkingLibwebsocketContext.websocketRxBufferLength += dataSize;
                    LogDebug( ( "Receive length %ld message from server, totally %ld", dataSize, networkingLibwebsocketContext.websocketRxBufferLength ) );
                }
                else
                {
                    LogWarn( ( "Buffer is not enough for received message, totally %ld", networkingLibwebsocketContext.websocketRxBufferLength + dataSize ) );
                    retValue = 1;
                    break;
                }

                /* callback to user if it's the final fragment. */
                if( lws_is_final_fragment(wsi) )
                {
                    if( networkingLibwebsocketContext.websocketMessageCallback != NULL )
                    {
                        networkingLibwebsocketContext.websocketMessageCallback( networkingLibwebsocketContext.websocketRxBuffer,
                                                                                networkingLibwebsocketContext.websocketRxBufferLength,
                                                                                networkingLibwebsocketContext.pWebsocketMessageCallbackContext );
                    }
                }
            }

            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:

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

WebsocketResult_t Websocket_Init( void * pCredential, WebsocketMessageCallback_t websocketMessageCallback, void *pCallbackContext )
{
    WebsocketResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    NetworkingLibwebsocketsCredentials_t *pNetworkingLibwebsocketsCredentials = (NetworkingLibwebsocketsCredentials_t *)pCredential;

    ret = NetworkingLibwebsockets_Init( pNetworkingLibwebsocketsCredentials );

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        networkingLibwebsocketContext.websocketMessageCallback = websocketMessageCallback;
        networkingLibwebsocketContext.pWebsocketMessageCallbackContext = pCallbackContext;
    }

    return ret;
}

WebsocketResult_t Websocket_Recv()
{
    WebsocketResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    
    ret = performLwsRecv();

    return ret;
}
