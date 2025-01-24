#include "logging.h"
#include "networkingLibwebsockets.h"
#include "networkingLibwebsockets_private.h"
#include "networking_utils.h"

#define NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME "X-Amz-ChannelARN"
#define NETWORKING_LWS_STRING_CREDENTIAL_PARAM_NAME "X-Amz-Credential"
#define NETWORKING_LWS_STRING_DATE_PARAM_NAME "X-Amz-Date"
#define NETWORKING_LWS_STRING_EXPIRES_PARAM_NAME "X-Amz-Expires"
#define NETWORKING_LWS_STRING_SIGNED_HEADERS_PARAM_NAME "X-Amz-SignedHeaders"
#define NETWORKING_LWS_STRING_SECURITY_TOKEN_PARAM_NAME "X-Amz-Security-Token"
#define NETWORKING_LWS_STRING_SIGNATURE_PARAM_NAME "X-Amz-Signature"
#define NETWORKING_LWS_STRING_SIGNED_HEADERS_VALUE "host"

#define NETWORKING_LWS_STRING_CREDENTIAL_VALUE_TEMPLATE "%.*s/%.*s/%.*s/" NETWORKING_LWS_KVS_SERVICE_NAME "/aws4_request"

#define NETWORKING_LWS_CREDENTIAL_PARAM_DATE_LENGTH ( 8 )
#define NETWORKING_LWS_STATIC_CRED_EXPIRES_SECONDS ( 604800 )

#ifndef MIN
#define MIN( a,b ) ( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#endif

#ifndef MAX
#define MAX( a,b ) ( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )
#endif

static NetworkingLibwebsocketsResult_t WriteUriEncodeAlgorithm( char ** ppBuffer,
                                                                size_t * pBufferLength )
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

static NetworkingLibwebsocketsResult_t WriteUriEncodeChannelArn( char ** ppBuffer,
                                                                 size_t * pBufferLength,
                                                                 char * pChannelArn,
                                                                 size_t channelArnLength )
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
        ret = UriEncode( *ppBuffer, writtenLength, ( *ppBuffer ) + writtenLength, &encodedLength );

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

static NetworkingLibwebsocketsResult_t WriteUriEncodeCredential( char ** ppBuffer,
                                                                 size_t * pBufferLength )
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
        writtenLength = snprintf( *ppBuffer,
                                  *pBufferLength,
                                  NETWORKING_LWS_STRING_CREDENTIAL_VALUE_TEMPLATE,
                                  ( int ) networkingLibwebsocketContext.libwebsocketsCredentials.accessKeyIdLength,
                                  networkingLibwebsocketContext.libwebsocketsCredentials.pAccessKeyId,
                                  NETWORKING_LWS_CREDENTIAL_PARAM_DATE_LENGTH,
                                  networkingLibwebsocketContext.appendHeaders.pDate,
                                  ( int ) networkingLibwebsocketContext.libwebsocketsCredentials.regionLength,
                                  networkingLibwebsocketContext.libwebsocketsCredentials.pRegion );

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
        ret = UriEncode( *ppBuffer, writtenLength, *ppBuffer + writtenLength, &encodedLength );

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

static NetworkingLibwebsocketsResult_t WriteUriEncodeDate( char ** ppBuffer,
                                                           size_t * pBufferLength )
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
        ret = UriEncode( *ppBuffer, writtenLength, *ppBuffer + writtenLength, &encodedLength );

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

static NetworkingLibwebsocketsResult_t WriteUriEncodeExpires( char ** ppBuffer,
                                                              size_t * pBufferLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t writtenLength;
    size_t encodedLength;
    uint64_t expirationSeconds;
    char * pCurrentTime;
    size_t dateLength;

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
        ret = GetIso8601CurrentTime( &pCurrentTime, &dateLength );

        if( networkingLibwebsocketContext.libwebsocketsCredentials.expirationSeconds == 0U )
        {
            /* Using permanent credential, set the expires to default value. */
            expirationSeconds = NETWORKING_LWS_STATIC_CRED_EXPIRES_SECONDS;
        }
        else
        {
            expirationSeconds = MIN( NETWORKING_LWS_STATIC_CRED_EXPIRES_SECONDS, networkingLibwebsocketContext.libwebsocketsCredentials.expirationSeconds - NetworkingUtils_GetCurrentTimeSec( NULL ) );
            expirationSeconds = MAX( expirationSeconds, 1 );
        }

        writtenLength = snprintf( *ppBuffer, *pBufferLength, "%lu", expirationSeconds );

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
        ret = UriEncode( *ppBuffer, writtenLength, *ppBuffer + writtenLength, &encodedLength );

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

static NetworkingLibwebsocketsResult_t WriteUriEncodeSignedHeaders( char ** ppBuffer,
                                                                    size_t * pBufferLength )
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

static NetworkingLibwebsocketsResult_t WriteUriEncodeSecurityToken( char ** ppBuffer,
                                                                    size_t * pBufferLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t writtenLength;
    size_t encodedLength;

    /* X-Amz-Security-Token query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        writtenLength = snprintf( *ppBuffer, *pBufferLength, "&" NETWORKING_LWS_STRING_SECURITY_TOKEN_PARAM_NAME "=" );


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

    /* X-Amz-Security-Token value (plaintext). */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {

        writtenLength = snprintf( *ppBuffer, *pBufferLength, "%.*s",
                                  ( int ) networkingLibwebsocketContext.libwebsocketsCredentials.sessionTokenLength, networkingLibwebsocketContext.libwebsocketsCredentials.pSessionToken );

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

    /* X-Amz-Security-Token value (URI encoded) */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        encodedLength = *pBufferLength - writtenLength;
        ret = UriEncode( *ppBuffer, writtenLength, *ppBuffer + writtenLength, &encodedLength );

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

static NetworkingLibwebsocketsResult_t GenerateQueryParameters( char * pQueryStart,
                                                                size_t queryLength,
                                                                char * pOutput,
                                                                size_t * pOutputLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char * pChannelArnQueryParam, * pChannelArnValue, * pEqual;
    size_t channelArnQueryParamLength, channelArnValueLength;
    char * pCurrentWrite = pOutput;
    size_t remainLength;

    if( ( pOutput == NULL ) || ( pOutputLength == NULL ) )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        if( ( queryLength < strlen( NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME ) ) ||
            ( strncmp( pQueryStart, NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME, strlen( NETWORKING_LWS_STRING_CHANNEL_ARN_PARAM_NAME ) ) != 0 ) )
        {
            /* No channel ARN exist. */
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_UNEXPECTED_WEBSOCKET_URL;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        /* Parse existing query parameters. */
        pEqual = strchr( pQueryStart, '=' );
        if( pEqual == NULL )
        {
            /* No equal found, unexpected. */
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_UNEXPECTED_WEBSOCKET_URL;
        }
        else
        {
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

        ret = WriteUriEncodeAlgorithm( &pCurrentWrite, &remainLength );
    }

    /* X-Amz-ChannelARN query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = WriteUriEncodeChannelArn( &pCurrentWrite, &remainLength, pChannelArnValue, channelArnValueLength );
    }

    /* X-Amz-Credential query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = WriteUriEncodeCredential( &pCurrentWrite, &remainLength );
    }

    /* X-Amz-Date query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = WriteUriEncodeDate( &pCurrentWrite, &remainLength );
    }

    /* X-Amz-Expires query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = WriteUriEncodeExpires( &pCurrentWrite, &remainLength );
    }

    // /* X-Amz-Security-Token query parameter. */
    if( ( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK ) && ( networkingLibwebsocketContext.libwebsocketsCredentials.sessionTokenLength > 0U ) )
    {
        ret = WriteUriEncodeSecurityToken( &pCurrentWrite, &remainLength );
    }

    /* X-Amz-SignedHeaders query parameter. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = WriteUriEncodeSignedHeaders( &pCurrentWrite, &remainLength );
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        *pOutputLength = *pOutputLength - remainLength;
    }

    return ret;
}

static NetworkingLibwebsocketsResult_t SignWebsocketRequest( WebsocketServerInfo_t * pWebsocketServerInfo )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    int32_t headerLength;
    NetworkingLibwebsocketsAppendHeaders_t * pAppendHeaders = &networkingLibwebsocketContext.appendHeaders;
    NetworkingLibwebsocketCanonicalRequest_t canonicalRequest;
    char * pPath, * pQueryStart, * pUrlEnd;
    size_t pathLength, queryLength, remainLength;
    size_t queryParamsStringLength;

    /* Find the path for request. */
    ret = GetPathFromUrl( pWebsocketServerInfo->pUrl, pWebsocketServerInfo->urlLength, &pPath, &pathLength );

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
        ret = GenerateQueryParameters( pQueryStart,
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

        ret = GenerateAuthorizationHeader( &canonicalRequest );
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

int32_t LwsWebsocketCallbackRoutine( struct lws * wsi,
                                     enum lws_callback_reasons reason,
                                     void * pUser,
                                     void * pDataIn,
                                     size_t dataSize )
{
    int32_t retValue = 0;
    int32_t skipProcess = 0;
    NetworkingLibwebsocketsResult_t websocketRet = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    int writeSize = 0;
    size_t remainSize = 0;
    size_t index;
    NetworkingLibwebsocketBufferInfo_t * pRingBufferInfo;

    LogVerbose( ( "Websocket callback with reason %d", reason ) );

    switch( reason )
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
            if( lws_frame_is_binary( wsi ) ||
                ( dataSize == 0 ) )
            {
                /* Binary data is not supported. */
                skipProcess = 1;
            }

            if( skipProcess == 0 )
            {
                /* Store the message into buffer. */
                // Check what type of a message it is. We will set the size to 0 on first and flush on last
                if( lws_is_first_fragment( wsi ) )
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
                if( lws_is_final_fragment( wsi ) )
                {
                    if( networkingLibwebsocketContext.websocketRxCallback != NULL )
                    {
                        networkingLibwebsocketContext.websocketRxCallback( networkingLibwebsocketContext.websocketRxBuffer,
                                                                           networkingLibwebsocketContext.websocketRxBufferLength,
                                                                           networkingLibwebsocketContext.pWebsocketRxCallbackContext );
                    }
                }
            }

            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            websocketRet = GetRingBufferCurrentIndex( &networkingLibwebsocketContext.websocketTxRingBuffer, &index );

            /* If the ring buffer is empty, it returns NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_EMPTY instead of NETWORKING_LIBWEBSOCKETS_RESULT_OK. */
            if( websocketRet == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
            {
                /* Have pending message to send. */
                pRingBufferInfo = &networkingLibwebsocketContext.websocketTxRingBuffer.bufferInfo[ index ];
                remainSize = pRingBufferInfo->bufferLength - pRingBufferInfo->offset;

                writeSize = lws_write( wsi,
                                       &pRingBufferInfo->buffer[ LWS_PRE + pRingBufferInfo->offset ],
                                       remainSize,
                                       LWS_WRITE_TEXT );

                if( writeSize < 0 )
                {
                    /* Some thing wrong in libwebsockets. */
                    LogError( ( "lws_write fail, which should never happen in writeable callback, result: %d.", writeSize ) );
                }
                else if( writeSize == remainSize )
                {
                    /* Entire message is sent successfully, free ring buffer. */
                    websocketRet = FreeRingBuffer( &networkingLibwebsocketContext.websocketTxRingBuffer, index );
                }
                else
                {
                    /* Partially send, update offset and trigger next write. */
                    pRingBufferInfo->offset += writeSize;
                    lws_callback_on_writable( wsi );
                }
            }
            break;
        default:
            break;
    }

    return retValue;
}

WebsocketResult_t Websocket_Connect( WebsocketServerInfo_t * pServerInfo )
{
    WebsocketResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char * pPath;
    size_t pathLength;

    memset( &networkingLibwebsocketContext.appendHeaders, 0, sizeof( NetworkingLibwebsocketsAppendHeaders_t ) );

    /* Append HTTP headers for signing.
     * Refer to https://docs.aws.amazon.com/AmazonECR/latest/APIReference/CommonParameters.html for details. */
    /* user-agent */
    networkingLibwebsocketContext.appendHeaders.pUserAgent = networkingLibwebsocketContext.libwebsocketsCredentials.pUserAgent;
    networkingLibwebsocketContext.appendHeaders.userAgentLength = networkingLibwebsocketContext.libwebsocketsCredentials.userAgentLength;

    /* host */
    ret = GetUrlHost( pServerInfo->pUrl, pServerInfo->urlLength, &networkingLibwebsocketContext.appendHeaders.pHost, &networkingLibwebsocketContext.appendHeaders.hostLength );

    /* x-amz-date */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = GetIso8601CurrentTime( &networkingLibwebsocketContext.appendHeaders.pDate, &networkingLibwebsocketContext.appendHeaders.dateLength );
    }

    /* Create query parameters. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = SignWebsocketRequest( pServerInfo );
    }

    /* Blocking execution until getting response from server. */
    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = PerformLwsConnect( networkingLibwebsocketContext.appendHeaders.pHost, networkingLibwebsocketContext.appendHeaders.hostLength, 443U, NETWORKING_LWS_HTTP_VERB_WSS );
    }

    return ( WebsocketResult_t ) ret;
}

WebsocketResult_t Websocket_Disconnect( void )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;

    lws_set_timeout( networkingLibwebsocketContext.pLws[ NETWORKING_LWS_PROTOCOLS_WEBSOCKET_INDEX ],
                     PENDING_TIMEOUT_CLOSE_SEND,
                     LWS_TO_KILL_SYNC );

    return ( WebsocketResult_t ) ret;
}

WebsocketResult_t Websocket_Init( void * pCredential,
                                  WebsocketMessageCallback_t rxCallback,
                                  void * pRxCallbackContext )
{
    WebsocketResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    NetworkingLibwebsocketsCredentials_t * pNetworkingLibwebsocketsCredentials = ( NetworkingLibwebsocketsCredentials_t * )pCredential;

    ret = NetworkingLibwebsockets_Init( pNetworkingLibwebsocketsCredentials );

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        networkingLibwebsocketContext.websocketRxCallback = rxCallback;
        networkingLibwebsocketContext.pWebsocketRxCallbackContext = pRxCallbackContext;
    }

    return ( WebsocketResult_t ) ret;
}

WebsocketResult_t Websocket_Send( char * pMessage,
                                  size_t messageLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t index;

    if( pMessage == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }
    else if( messageLength > NETWORKING_LWS_RING_BUFFER_LENGTH - LWS_PRE )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_TOO_SMALL;
    }
    else
    {
        /* Do nothing, coverity happy. */
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        ret = AllocateRingBuffer( &networkingLibwebsocketContext.websocketTxRingBuffer, &index );
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        memcpy( &networkingLibwebsocketContext.websocketTxRingBuffer.bufferInfo[ index ].buffer[ LWS_PRE ], pMessage, messageLength );
        networkingLibwebsocketContext.websocketTxRingBuffer.bufferInfo[ index ].bufferLength = messageLength;
        networkingLibwebsocketContext.websocketTxRingBuffer.bufferInfo[ index ].offset = 0U;

        lws_callback_on_writable( networkingLibwebsocketContext.pLws[ NETWORKING_LWS_PROTOCOLS_WEBSOCKET_INDEX ] );
    }

    return ( WebsocketResult_t ) ret;
}

WebsocketResult_t Websocket_Recv()
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;

    ret = PerformLwsRecv();

    return ( WebsocketResult_t ) ret;
}

WebsocketResult_t Websocket_Signal()
{
    /* wake the thread running lws_service() up to handle events. */
    NetworkingLibwebsockets_Signal( networkingLibwebsocketContext.pLwsContext );

    return ( WebsocketResult_t ) NETWORKING_LIBWEBSOCKETS_RESULT_OK;
}

WebsocketResult_t Websocket_UpdateCredential( void * pCredential )
{
    return UpdateCredential( ( NetworkingLibwebsocketsCredentials_t * ) pCredential );
}
