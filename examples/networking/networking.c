/* Interface includes. */
#include "networking.h"

/* SigV4 headers. */
#include "sigv4.h"

/*----------------------------------------------------------------------------*/

#define STATIC_CRED_EXPIRES_SECONDS ( 604800 )
#ifndef MIN
    #define MIN( a, b ) ( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#endif

#ifndef MAX
    #define MAX( a, b ) ( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )
#endif

/*----------------------------------------------------------------------------*/

static int LwsHttpCallback( struct lws * pWsi,
                            enum lws_callback_reasons reason,
                            void * pUser,
                            void * pData,
                            size_t dataLength );

static int LwsWebsocketCallback( struct lws * pWsi,
                                 enum lws_callback_reasons reason,
                                 void * pUser,
                                 void * pData,
                                 size_t dataLength );

/*----------------------------------------------------------------------------*/

static int32_t Sha256Init( void * pHashContext )
{
    int32_t ret = 0;
    const EVP_MD * md = EVP_sha256();

    ret = EVP_DigestInit( pHashContext, md );

    if( ret != 1 )
    {
        LogError( ( "Failed to initialize EVP_sha256!" ) );
    }

    return ret == 1 ? 0 : -1;
}

/*----------------------------------------------------------------------------*/

static int32_t Sha256Update( void * pHashContext,
                             const uint8_t * pInput,
                             size_t inputLen )
{
    int32_t ret = 0;

    ret = EVP_DigestUpdate( pHashContext, pInput, inputLen );

    if( ret != 1 )
    {
        LogError( ( "Failed to update EVP_sha256!" ) );
    }

    return ret == 1 ? 0 : -1;
}

/*----------------------------------------------------------------------------*/

static int32_t Sha256Final( void * pHashContext,
                            uint8_t * pOutput,
                            size_t outputLen )
{
    int32_t ret = 0;
    unsigned int outLength = outputLen;

    ret = EVP_DigestFinal( pHashContext, pOutput, &( outLength ) );

    if( ret != 1 )
    {
        LogError( ( "Failed to finalize EVP_sha256!" ) );
    }

    return ret == 1 ? 0 : -1;
}

/*----------------------------------------------------------------------------*/

static int GetHostFromUrl( char * pUrl,
                           size_t urlLength,
                           char ** ppHost,
                           size_t * pHostLength )
{
    int ret = 0;
    char * pStart = NULL, * pEnd = NULL, * pCurPtr;
    uint8_t foundEndMark = 0;

    pEnd = pUrl + urlLength;

    if( ret == 0 )
    {
        pStart = strstr( pUrl, "://" );
        if( pStart == NULL )
        {
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        /* Move past "://". */
        pStart += 3;

        if( pStart > pEnd )
        {
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        /* Find the delimiter which would indicate end of the host - either one
         * of "/", ":" and "?". */
        pCurPtr = pStart;

        while( ( foundEndMark == 0 ) && ( pCurPtr < pEnd ) )
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
                    break;
            }
        }
    }

    if( ( ret == 0 ) && ( foundEndMark == 1 ) )
    {
        *ppHost = pStart;
        *pHostLength = pCurPtr - pStart;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static int GetPathFromUrl( char * pUrl,
                           size_t urlLength,
                           char ** ppPath,
                           size_t * pPathLength )
{
    int ret = 0;
    char * pHost = NULL, * pPathStart, * pPathEnd, * pQueryStart;
    size_t hostLength = 0;

    /* Find out the host part. */
    ret = GetHostFromUrl( pUrl, urlLength, &( pHost ), &( hostLength ) );

    if( ret == 0 )
    {
        pPathStart = pHost + hostLength;

        /* Query portion starts with '?'. */
        pQueryStart = strchr( pPathStart, '?' );

        if( pQueryStart == NULL )
        {
            /* There is no query and all the remaining part after the host
             * belongs to the path. */
            pPathEnd = pUrl + urlLength;
        }
        else
        {
            /* The part after the host until '?' belongs to the path. */
            pPathEnd = pQueryStart;
        }

        *ppPath = pPathStart;
        *pPathLength = pPathEnd - pPathStart;
    }
    else
    {
        LogError( ( "Failed to extract host from the URL! URL: %.*s",
                    (int) urlLength,
                    pUrl ) );
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static int GetCurrentTimeInIso8601Format( char * pBuf,
                                          size_t * pBufLength )
{
    int ret = 0;
    time_t now;
    size_t timeLength = 0;

    time( &( now ) );
    timeLength = strftime( pBuf, *pBufLength, "%Y%m%dT%H%M%SZ", gmtime( &( now ) ) );

    if( timeLength <= 0 )
    {
        LogError( ( "Fail to strftime, length: %lu", timeLength ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        *pBufLength = timeLength;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static int UriEncode( const char * pSrc,
                      size_t srcLength,
                      char * pDst,
                      size_t * pDstLength )
{
    int ret = 0;
    char ch;
    size_t i, j, remainingLength = *pDstLength;
    const char alpha[ 17 ] = "0123456789ABCDEF";

    for( i = 0, j = 0; i < srcLength; i++ )
    {
        ch = pSrc[ i ];

        if( ( ( ch >= 'A' ) && ( ch <= 'Z' ) ) ||
            ( ( ch >= 'a' ) && ( ch <= 'z' ) ) ||
            ( ( ch >= '0' ) && ( ch <= '9' ) ) ||
            ( ch == '_' ) ||
            ( ch == '-' ) ||
            ( ch == '~' ) ||
            ( ch == '.' ) )
        {
            if( remainingLength < 1 )
            {
                ret = -1;
                break;
            }
            else
            {
                pDst[ j ] = ch;
                j++;
                remainingLength -= 1;
            }
        }
        else if( ch == '/' )
        {
            if( remainingLength < 3 )
            {
                ret = -1;
                break;
            }
            else
            {
                pDst[ j ] = '%';
                pDst[ j + 1 ] = '2';
                pDst[ j + 2 ] = 'F';
                j += 3;
                remainingLength -= 3;
            }
        }
        else
        {
            if( remainingLength < 3 )
            {
                ret = -1;
                break;
            }
            else
            {
                pDst[ j ] = '%';
                pDst[ j + 1 ] = alpha[ ch >> 4 ];
                pDst[ j + 2 ] = alpha[ ch & 0x0F ];
                j += 3;
                remainingLength -= 3;
            }
        }
    }

    if( ret == 0 )
    {
        *pDstLength = j;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static int SignHttpRequest( NetworkingContext_t * pCtx,
                            HttpRequest_t * pRequest,
                            const AwsCredentials_t * pAwsCredentials )
{
    int ret = 0, snprintfRetVal;
    char * pSignature;
    size_t signatureLength;
    SigV4HttpParameters_t sigv4HttpParams;
    SigV4Parameters_t sigv4Params;
    SigV4Credentials_t sigv4Credentials;
    SigV4CryptoInterface_t sigv4CryptoInterface;

    snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ 0 ] ),
                               SIGV4_METADATA_BUFFER_LENGTH,
                               "host: %.*s\r\n%s: %.*s\r\n%s: %.*s\r\n",
                               ( int ) pCtx->uriHostLength,
                               &( pCtx->uriHost[ 0 ] ),
                               pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_USER_AGENT_IDX ].pName,
                               ( int ) pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_USER_AGENT_IDX ].valueLength,
                               pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_USER_AGENT_IDX ].pValue,
                               pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_ISO8601_TIME_IDX ].pName,
                               ( int ) pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_ISO8601_TIME_IDX ].valueLength,
                               pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_ISO8601_TIME_IDX ].pValue );

    if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == SIGV4_METADATA_BUFFER_LENGTH ) )
    {
        ret = -1;
    }

    if( ret == 0 )
    {
        sigv4HttpParams.pHttpMethod = ( pRequest->verb == HTTP_POST ) ? "POST" : "GET";
        sigv4HttpParams.httpMethodLen = strlen( sigv4HttpParams.pHttpMethod );
        sigv4HttpParams.flags = 0;
        sigv4HttpParams.pPath = &( pCtx->uriPath[ 0 ] );
        sigv4HttpParams.pathLen = pCtx->uriPathLength;
        sigv4HttpParams.pQuery = NULL;
        sigv4HttpParams.queryLen = 0;
        sigv4HttpParams.pHeaders = &( pCtx->sigV4Metadata[ 0 ] );
        sigv4HttpParams.headersLen = snprintfRetVal;
        sigv4HttpParams.pPayload = pRequest->pBody;
        sigv4HttpParams.payloadLen = pRequest->bodyLength;

        sigv4Credentials.pAccessKeyId = pAwsCredentials->pAccessKeyId;
        sigv4Credentials.accessKeyIdLen = pAwsCredentials->accessKeyIdLen;
        sigv4Credentials.pSecretAccessKey = pAwsCredentials->pSecretAccessKey;
        sigv4Credentials.secretAccessKeyLen = pAwsCredentials->secretAccessKeyLen;

        sigv4CryptoInterface.hashInit = Sha256Init;
        sigv4CryptoInterface.hashUpdate = Sha256Update;
        sigv4CryptoInterface.hashFinal = Sha256Final;
        sigv4CryptoInterface.pHashContext = EVP_MD_CTX_new();
        sigv4CryptoInterface.hashBlockLen = 64;
        sigv4CryptoInterface.hashDigestLen = 32;

        sigv4Params.pCredentials = &( sigv4Credentials );
        sigv4Params.pDateIso8601 = pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_ISO8601_TIME_IDX ].pValue;
        sigv4Params.pAlgorithm = NULL;
        sigv4Params.algorithmLen = 0;
        sigv4Params.pRegion = pAwsCredentials->pRegion;
        sigv4Params.regionLen = pAwsCredentials->regionLen;
        sigv4Params.pService = pAwsCredentials->pService;
        sigv4Params.serviceLen = pAwsCredentials->serviceLen;
        sigv4Params.pCryptoInterface = &( sigv4CryptoInterface );
        sigv4Params.pHttpParameters = &( sigv4HttpParams );

        pCtx->sigv4AuthorizationHeaderLength = SIGV4_AUTHORIZATION_HEADER_BUFFER_LENGTH;

        if( SigV4_GenerateHTTPAuthorization( &( sigv4Params ),
                                             &( pCtx->sigv4AuthorizationHeader[ 0 ] ),
                                             &( pCtx->sigv4AuthorizationHeaderLength ),
                                             &( pSignature ),
                                             &( signatureLength ) ) != SigV4Success )
        {
            LogError( ( "Failed to generate SigV4 authorization header!" ) );
            ret = -1;
        }

        EVP_MD_CTX_free( sigv4CryptoInterface.pHashContext );
    }

    if( ret == 0 )
    {
        pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_AUTHORIZATION_IDX ].pName = "Authorization";
        pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_AUTHORIZATION_IDX ].pValue = &( pCtx->sigv4AuthorizationHeader[ 0 ] );
        pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_AUTHORIZATION_IDX ].valueLength = pCtx->sigv4AuthorizationHeaderLength;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static int SignWebsocketRequest( NetworkingContext_t * pCtx,
                                 const WebsocketConnectInfo_t * pConnectInfo,
                                 const AwsCredentials_t * pAwsCredentials )
{
    int ret = 0, snprintfRetVal;
    char * pPath, * pQueryStart, * pUrlEnd, * pEqualSign, * pChannelArnValue, * pSignature;
    size_t pathLength, queryLength, remainingLength, writtenLength, signatureLength;
    size_t channelArnValueLength, encodedLength, canonicalQueryStringLength, canonicalHeadersLength;
    SigV4HttpParameters_t sigv4HttpParams;
    SigV4Parameters_t sigv4Params;
    SigV4Credentials_t sigv4Credentials;
    SigV4CryptoInterface_t sigv4CryptoInterface;

    writtenLength = 0;
    remainingLength = SIGV4_METADATA_BUFFER_LENGTH;

    /* Write X-Amz-Algorithm. */
    snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                               remainingLength,
                               "X-Amz-Algorithm=AWS4-HMAC-SHA256" );

    if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
    {
        LogError( ( "Failed to write X-Amz-Algorithm!" ) );
        ret = -1;
    }
    else
    {
        writtenLength += snprintfRetVal;
        remainingLength -= snprintfRetVal;
    }

    /* Write X-Amz-ChannelARN. */
    if( ret == 0 )
    {
        ret = GetPathFromUrl( pConnectInfo->pUrl,
                              pConnectInfo->urlLength,
                              &( pPath ),
                              &( pathLength ) );

        if( ret == 0 )
        {
            pQueryStart = pPath + pathLength + 1; /* +1 to skip '?' mark. */
            pUrlEnd = pConnectInfo->pUrl + pConnectInfo->urlLength;

            if( pQueryStart < pUrlEnd )
            {
                queryLength = pUrlEnd - pQueryStart;
            }
            else
            {
                LogError( ( "Cannot find query string in the URL!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            if( ( queryLength < strlen( "X-Amz-ChannelARN" ) ) ||
                ( strncmp( pQueryStart, "X-Amz-ChannelARN", strlen( "X-Amz-ChannelARN" ) ) != 0 ) )
            {
                LogError( ( "Cannot find X-Amz-ChannelARN in the query string!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            pEqualSign = strchr( pQueryStart, '=' );

            if( pEqualSign != NULL )
            {
                pChannelArnValue = pEqualSign + 1;
                channelArnValueLength = pQueryStart + queryLength - pChannelArnValue;
            }
            else
            {
                LogError( ( "Cannot find = after X-Amz-ChannelARN in the query string!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                                       remainingLength,
                                       "&X-Amz-ChannelARN=" );
            if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
            {
                LogError( ( "Failed to write X-Amz-ChannelARN key!" ) );
                ret = -1;
            }
            else
            {
                writtenLength += snprintfRetVal;
                remainingLength -= snprintfRetVal;
            }
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( pChannelArnValue,
                             channelArnValueLength,
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-ChannelARN value!" ) );
                ret = -1;
            }
        }
    }

    /* Write X-Amz-Credential. */
    if( ret == 0 )
    {
        snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                                   remainingLength,
                                   "&X-Amz-Credential=" );
        if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
        {
            LogError( ( "Failed to write X-Amz-Credential key!" ) );
            ret = -1;
        }
        else
        {
            writtenLength += snprintfRetVal;
            remainingLength -= snprintfRetVal;
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( pAwsCredentials->pAccessKeyId,
                             pAwsCredentials->accessKeyIdLen,
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Credential access key!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( "/",
                             1,
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Credential separator!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( &( pCtx->iso8601Time[ 0 ] ),
                             8, /* Only the date part. */
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Credential date!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( "/",
                             1,
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Credential separator!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( pAwsCredentials->pRegion,
                             pAwsCredentials->regionLen,
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Credential region!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( "/",
                             1,
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Credential separator!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( pAwsCredentials->pService,
                             pAwsCredentials->serviceLen,
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Credential service!" ) );
                ret = -1;
            }
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( "/aws4_request",
                             strlen( "/aws4_request" ),
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Credential aws4_request!" ) );
                ret = -1;
            }
        }
    }

    /* Write X-Amz-Date. */
    if( ret == 0 )
    {
        snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                                   remainingLength,
                                   "&X-Amz-Date=%.*s",
                                   ( int ) pCtx->iso8601TimeLength,
                                   &( pCtx->iso8601Time[ 0 ] ) );
        if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
        {
            LogError( ( "Failed to write X-Amz-Date!" ) );
            ret = -1;
        }
        else
        {
            writtenLength += snprintfRetVal;
            remainingLength -= snprintfRetVal;
        }
    }

    /* Write X-Amz-Expires. */
    if( ret == 0 )
    {
        uint64_t expirationSeconds = STATIC_CRED_EXPIRES_SECONDS;

        if( pAwsCredentials->expirationSeconds != 0 )
        {
            expirationSeconds = MIN( STATIC_CRED_EXPIRES_SECONDS,
                                     pAwsCredentials->expirationSeconds - time( NULL ) );
            expirationSeconds = MAX( expirationSeconds, 1 );
        }

        snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                                   remainingLength,
                                   "&X-Amz-Expires=%lu",
                                   expirationSeconds );
        if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
        {
            LogError( ( "Failed to write X-Amz-Expires!" ) );
            ret = -1;
        }
        else
        {
            writtenLength += snprintfRetVal;
            remainingLength -= snprintfRetVal;
        }
    }

    /* Write X-Amz-Security-Token. */
    if( ( ret == 0 ) &&
        ( pAwsCredentials->pSessionToken != NULL ) &&
        ( pAwsCredentials->sessionTokenLength > 0 ) )
    {
        snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                                   remainingLength,
                                   "&X-Amz-Security-Token=" );
        if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
        {
            LogError( ( "Failed to write X-Amz-Security-Token key!" ) );
            ret = -1;
        }
        else
        {
            writtenLength += snprintfRetVal;
            remainingLength -= snprintfRetVal;
        }

        if( ret == 0 )
        {
            encodedLength = remainingLength;
            ret = UriEncode( pAwsCredentials->pSessionToken,
                             pAwsCredentials->sessionTokenLength,
                             &( pCtx->sigV4Metadata[ writtenLength ] ),
                             &( encodedLength ) );
            if( ret == 0 )
            {
                writtenLength += encodedLength;
                remainingLength -= encodedLength;
            }
            else
            {
                LogError( ( "Failed to write X-Amz-Security-Token value!" ) );
                ret = -1;
            }
        }
    }

    /* Write X-Amz-SignedHeaders. */
    if( ret == 0 )
    {
        snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                                   remainingLength,
                                   "&X-Amz-SignedHeaders=host" );
        if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
        {
            LogError( ( "Failed to write X-Amz-SignedHeaders!" ) );
            ret = -1;
        }
        else
        {
            writtenLength += snprintfRetVal;
            remainingLength -= snprintfRetVal;
        }
    }

    /* Write canonical headers. */
    if( ret == 0 )
    {
        canonicalQueryStringLength = writtenLength;

        snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                                   remainingLength,
                                   "%s: %.*s\r\n",
                                   "host",
                                   ( int ) pCtx->uriHostLength,
                                   &( pCtx->uriHost[ 0 ] ) );

        if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
        {
            LogError( ( "Failed to write canonical headers!" ) );
            ret = -1;
        }
        else
        {
            canonicalHeadersLength = snprintfRetVal;
        }
    }

    /* Generate signature. */
    if( ret == 0 )
    {
        sigv4HttpParams.pHttpMethod = "GET";
        sigv4HttpParams.httpMethodLen = 3;
        sigv4HttpParams.flags = SIGV4_HTTP_QUERY_IS_CANONICAL_FLAG;
        sigv4HttpParams.pPath = pPath;
        sigv4HttpParams.pathLen = pathLength;
        sigv4HttpParams.pQuery = &( pCtx->sigV4Metadata[ 0 ] );
        sigv4HttpParams.queryLen = canonicalQueryStringLength;
        sigv4HttpParams.pHeaders = &( pCtx->sigV4Metadata[ canonicalQueryStringLength ] );
        sigv4HttpParams.headersLen = canonicalHeadersLength;
        sigv4HttpParams.pPayload = NULL;
        sigv4HttpParams.payloadLen = 0;

        sigv4Credentials.pAccessKeyId = pAwsCredentials->pAccessKeyId;
        sigv4Credentials.accessKeyIdLen = pAwsCredentials->accessKeyIdLen;
        sigv4Credentials.pSecretAccessKey = pAwsCredentials->pSecretAccessKey;
        sigv4Credentials.secretAccessKeyLen = pAwsCredentials->secretAccessKeyLen;

        sigv4CryptoInterface.hashInit = Sha256Init;
        sigv4CryptoInterface.hashUpdate = Sha256Update;
        sigv4CryptoInterface.hashFinal = Sha256Final;
        sigv4CryptoInterface.pHashContext = EVP_MD_CTX_new();
        sigv4CryptoInterface.hashBlockLen = 64;
        sigv4CryptoInterface.hashDigestLen = 32;

        sigv4Params.pCredentials = &( sigv4Credentials );
        sigv4Params.pDateIso8601 = &( pCtx->iso8601Time[ 0 ] );
        sigv4Params.pAlgorithm = NULL;
        sigv4Params.algorithmLen = 0;
        sigv4Params.pRegion = pAwsCredentials->pRegion;
        sigv4Params.regionLen = pAwsCredentials->regionLen;
        sigv4Params.pService = pAwsCredentials->pService;
        sigv4Params.serviceLen = pAwsCredentials->serviceLen;
        sigv4Params.pCryptoInterface = &( sigv4CryptoInterface );
        sigv4Params.pHttpParameters = &( sigv4HttpParams );

        pCtx->sigv4AuthorizationHeaderLength = SIGV4_AUTHORIZATION_HEADER_BUFFER_LENGTH;

        if( SigV4_GenerateHTTPAuthorization( &( sigv4Params ),
                                             &( pCtx->sigv4AuthorizationHeader[ 0 ] ),
                                             &( pCtx->sigv4AuthorizationHeaderLength ),
                                             &( pSignature ),
                                             &( signatureLength ) ) != SigV4Success )
        {
            LogError( ( "Failed to generate SigV4 authorization!" ) );
            ret = -1;
        }

        EVP_MD_CTX_free( sigv4CryptoInterface.pHashContext );
    }

    /* Append signature. */
    if( ret == 0 )
    {
        writtenLength = canonicalQueryStringLength;
        remainingLength = SIGV4_METADATA_BUFFER_LENGTH - canonicalQueryStringLength;

        snprintfRetVal = snprintf( &( pCtx->sigV4Metadata[ writtenLength ] ),
                                   remainingLength,
                                   "&X-Amz-Signature=%.*s",
                                   ( int ) signatureLength,
                                   pSignature );

        if( ( snprintfRetVal < 0 ) || ( snprintfRetVal == remainingLength ) )
        {
            LogError( ( "Failed to write X-Amz-Signature!" ) );
            ret = -1;
        }
        else
        {
            writtenLength += snprintfRetVal;
            remainingLength -= snprintfRetVal;
        }
    }

    /* Update UriPath buffer. This will be used during connect. */
    if( ret == 0 )
    {
        if( ( writtenLength + 2 ) <= URI_PATH_BUFFER_LENGTH )
        {
            pCtx->uriPathLength = writtenLength + 2;
            pCtx->uriPath[ 0 ] = '/';
            pCtx->uriPath[ 1 ] = '?';
            memcpy( &( pCtx->uriPath[ 2 ] ),
                    &( pCtx->sigV4Metadata[ 0 ] ),
                    pCtx->uriPathLength );
            pCtx->uriPath[ pCtx->uriPathLength ] = '\0';
        }
        else
        {
            LogError( ( "Failed to update UriPath buffer!" ) );
            ret = -1;
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static int LwsHttpCallback( struct lws * pWsi,
                            enum lws_callback_reasons reason,
                            void * pUser,
                            void * pData,
                            size_t dataLength )
{
    int ret = 0, bufferLength, writtenBodyLength, status, lwsStatus = 0;
    unsigned char * pEnd;
    unsigned char ** ppStart;
    size_t i;
    const struct lws_protocols * pLwsProtocol = NULL;
    HttpContext_t * pHttpContext = NULL;
    char contentLengthStr[ 11 ];
    size_t contentLengthStrLength;

    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            pLwsProtocol = lws_get_protocol( pWsi );
            pHttpContext = ( HttpContext_t * ) pLwsProtocol->user;
            pHttpContext->connectionClosed = 1;
            LogError( ( "HTTP connection error!" ) );
        }
        break;

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
        {
            pLwsProtocol = lws_get_protocol( pWsi );
            pHttpContext = ( HttpContext_t * ) pLwsProtocol->user;
            pHttpContext->connectionClosed = 1;
            LogDebug( ( "HTTP connection closed." ) );
        }
        break;

        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
        {
            status = lws_http_client_http_response( pWsi );
            ( void ) status;
            LogDebug( ( "Connected with HTTP server. Response: %d.", status ) );
        }
        break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
        {
            pLwsProtocol = lws_get_protocol( pWsi );
            pHttpContext = ( HttpContext_t * ) pLwsProtocol->user;

            LogDebug( ( "Received HTTP %lu bytes.", dataLength ) );
            LogVerbose( ( "Received HTTP data: %.*s", ( int ) dataLength, ( const char * ) pData ) );

            if( dataLength != 0 )
            {
                if( dataLength >= pHttpContext->pResponse->bufferLength )
                {
                    /* Receive data is larger than buffer size. */
                    ret = -2;
                    LogError( ( "Received HTTP data cannot fit in the response buffer!" ) );
                }
                else
                {
                    memcpy( pHttpContext->pResponse->pBuffer, pData, dataLength );
                    pHttpContext->pResponse->bufferLength = dataLength;
                }
            }
        }
        break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
        {
            pLwsProtocol = lws_get_protocol( pWsi );
            pHttpContext = ( HttpContext_t * ) pLwsProtocol->user;

            LogDebug( ( "LWS_CALLBACK_RECEIVE_CLIENT_HTTP callback." ) );

            bufferLength = HTTP_RX_BUFFER_LENGTH;

            if( lws_http_client_read( pWsi, ( char ** ) pHttpContext->rxBuffer, &( bufferLength ) ) < 0 )
            {
                LogError( ( "lws_http_client_read failed!" ) );
                ret = -1;
            }
        }
        break;

        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
        {
            LogDebug( ( "LWS_CALLBACK_COMPLETED_CLIENT_HTTP callback." ) );
        }
        break;

        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
        {
            pLwsProtocol = lws_get_protocol( pWsi );
            pHttpContext = ( HttpContext_t * ) pLwsProtocol->user;

            LogDebug( ( "LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER callback." ) );

            ppStart = ( unsigned char ** ) pData;
            pEnd = *ppStart + dataLength - 1;

            for( i = 0; i < NUM_REQUIRED_HEADERS; i++ )
            {
                if( pHttpContext->requiredHeaders[ i ].valueLength > 0 )
                {
                    lwsStatus = lws_add_http_header_by_name( pWsi,
                                                             ( const unsigned char * ) pHttpContext->requiredHeaders[ i ].pName,
                                                             ( const unsigned char * ) pHttpContext->requiredHeaders[ i ].pValue,
                                                             pHttpContext->requiredHeaders[ i ].valueLength,
                                                             ppStart,
                                                             pEnd );
                }
            }

            lwsStatus = lws_add_http_header_by_name( pWsi,
                                                     ( const unsigned char * ) "content-type",
                                                     ( const unsigned char * ) "application/json",
                                                     strlen( "application/json" ),
                                                     ppStart,
                                                     pEnd );

            contentLengthStrLength = snprintf( &( contentLengthStr[ 0 ] ), 11, "%lu", pHttpContext->pRequest->bodyLength );
            lwsStatus = lws_add_http_header_by_name( pWsi,
                                                     ( const unsigned char * ) "content-length",
                                                     ( const unsigned char * ) &( contentLengthStr[ 0 ] ),
                                                     contentLengthStrLength,
                                                     ppStart,
                                                     pEnd );

            for( i = 0; i < pHttpContext->pRequest->numHeaders; i++ )
            {
                lwsStatus = lws_add_http_header_by_name( pWsi,
                                                         ( const unsigned char * ) pHttpContext->pRequest->pHeaders[ i ].pName,
                                                         ( const unsigned char * ) pHttpContext->pRequest->pHeaders[ i ].pValue,
                                                         pHttpContext->pRequest->pHeaders[ i ].valueLength,
                                                         ppStart,
                                                         pEnd );
            }

            lws_client_http_body_pending( pWsi, 1 );
            lws_callback_on_writable( pWsi );
        }
        break;

        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
        {
            pLwsProtocol = lws_get_protocol( pWsi );
            pHttpContext = ( HttpContext_t * ) pLwsProtocol->user;

            LogDebug( ( "LWS_CALLBACK_CLIENT_HTTP_WRITEABLE callback." ) );

            writtenBodyLength = lws_write( pWsi,
                                           ( unsigned char * ) pHttpContext->pRequest->pBody,
                                           pHttpContext->pRequest->bodyLength,
                                           LWS_WRITE_TEXT );

            if( writtenBodyLength != pHttpContext->pRequest->bodyLength )
            {
                if( writtenBodyLength > 0 )
                {
                    /* Schedule again. */
                    lws_client_http_body_pending( pWsi, 1 );
                    lws_callback_on_writable( pWsi );
                }
                else
                {
                    /* Quit. */
                    ret = 1;
                    LogError( ( "lws_write failed!" ) );
                }
            }
            else
            {
                /* Finished sending the body. */
                lws_client_http_body_pending( pWsi, 0 );
            }
        }
        break;

        case LWS_CALLBACK_WSI_DESTROY:
        {
            pLwsProtocol = lws_get_protocol( pWsi );
            pHttpContext = ( HttpContext_t * ) pLwsProtocol->user;

            LogDebug( ( "LWS_CALLBACK_WSI_DESTROY callback." ) );

            pHttpContext->connectionClosed = 1;
        }
        break;

        default:
            break;
    }

    if( ( lwsStatus != 0 ) && ( ret == 0 ) )
    {
        ret = 1;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

static int LwsWebsocketCallback( struct lws * pWsi,
                                 enum lws_callback_reasons reason,
                                 void * pUser,
                                 void * pData,
                                 size_t dataLength )
{
    int ret = 0;
    const struct lws_protocols * pLwsProtocol = NULL;
    WebsocketContext_t * pWebsocketContext = NULL;

    pLwsProtocol = lws_get_protocol( pWsi );
    pWebsocketContext = ( WebsocketContext_t * ) pLwsProtocol->user;

    switch( reason )
    {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            pWebsocketContext->connectionEstablished = 0;
            pWebsocketContext->connectionClosed = 1;
            LogError( ( "LWS_CALLBACK_CLIENT_CONNECTION_ERROR callback!" ) );
        }
        break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            pWebsocketContext->connectionEstablished = 1;
            LogDebug( ( "WSS connection established." ) );
        }
        break;

        case LWS_CALLBACK_CLIENT_CLOSED:
        {
            pWebsocketContext->connectionEstablished = 0;
            pWebsocketContext->connectionClosed = 1;
            LogDebug( ( "WSS client closed the connection." ) );
        }
        break;

        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
        {
            pWebsocketContext->connectionEstablished = 0;
            pWebsocketContext->connectionClosed = 1;
            LogDebug( ( "WSS peer closed the connection." ) );
        }
        break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            LogDebug( ( "LWS_CALLBACK_CLIENT_RECEIVE callback." ) );

            if( ( lws_frame_is_binary( pWsi ) == 0 ) &&
                ( dataLength > 0 ) )
            {
                if( lws_is_first_fragment( pWsi ) )
                {
                    pWebsocketContext->dataLengthInRxBuffer = 0;
                }

                if( ( pWebsocketContext->dataLengthInRxBuffer + dataLength ) <= WEBSOCKET_RX_BUFFER_LENGTH )
                {
                    memcpy( &( pWebsocketContext->rxBuffer[ pWebsocketContext->dataLengthInRxBuffer ] ),
                            pData,
                            dataLength );
                    pWebsocketContext->dataLengthInRxBuffer += dataLength;

                    LogDebug( ( "Received %lu bytes of WSS message. Total %lu bytes received so far.",
                                dataLength,
                                pWebsocketContext->dataLengthInRxBuffer ) );

                    if( lws_is_final_fragment( pWsi ) )
                    {
                        if( pWebsocketContext->rxCallback != NULL )
                        {
                            ret = pWebsocketContext->rxCallback( &( pWebsocketContext->rxBuffer[ 0 ] ),
                                                                 pWebsocketContext->dataLengthInRxBuffer,
                                                                 pWebsocketContext->pRxCallbackData );
                        }
                    }
                }
                else
                {
                    LogWarn( ( "WSS RX buffer is not large enough for received message. Message size: %lu.",
                               pWebsocketContext->dataLengthInRxBuffer + dataLength ) );
                    ret = 1;
                }
            }
        }
        break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            RingBufferElement_t * pElement;
            RingBufferResult_t ringBufferResult;
            size_t remainingLength;
            int writtenLength;

            LogDebug( ( "LWS_CALLBACK_CLIENT_WRITEABLE callback." ) );

            ringBufferResult = RingBuffer_GetHeadEntry( &( pWebsocketContext->ringBuffer ),
                                                        &( pElement ) );

            if( ringBufferResult == RING_BUFFER_RESULT_OK )
            {
                remainingLength = pElement->bufferLength - pElement->currentIndex;

                writtenLength = lws_write( pWsi,
                                           ( unsigned char * ) &( pElement->pBuffer[ LWS_PRE + pElement->currentIndex ] ),
                                           remainingLength,
                                           LWS_WRITE_TEXT );

                if( writtenLength < 0 )
                {
                    LogError( ( "lws_write failed in LWS_CALLBACK_CLIENT_WRITEABLE callback, result: %d!", writtenLength ) );
                    ret = 1;
                }
                else if( writtenLength == remainingLength )
                {
                    free( pElement->pBuffer );
                    if( RingBuffer_RemoveHeadEntry( &( pWebsocketContext->ringBuffer ),
                                                    pElement ) != RING_BUFFER_RESULT_OK )
                    {
                        LogError( ( "Failed to remove element from the ring buffer!" ) );
                        ret = 1;
                    }
                }
                else
                {
                    pElement->currentIndex += writtenLength;
                    lws_callback_on_writable( pWsi );
                }
            }
        }
        break;

        case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        {
            LogDebug( ( "LWS_CALLBACK_EVENT_WAIT_CANCELLED callback." ) );

            if( pWebsocketContext->connectionEstablished == 1 )
            {
                lws_callback_on_writable( pWebsocketContext->pWsi );
            }
        }
        break;

        default:
            break;
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

NetworkingResult_t Networking_Init( NetworkingContext_t * pCtx,
                                    const SSLCredentials_t * pCreds )
{
    NetworkingResult_t ret = NETWORKING_RESULT_OK;
    struct lws_context_creation_info creationInfo;
    const lws_retry_bo_t retryPolicy =
    {
        .secs_since_valid_ping = 10,
        .secs_since_valid_hangup = 7200,
    };

    if( ( pCtx == NULL ) || ( pCreds == NULL ) )
    {
        ret = NETWORKING_RESULT_BAD_PARAM;
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        memset( pCtx, 0, sizeof( NetworkingContext_t ) );

        if( RingBuffer_Init( &( pCtx->websocketContext.ringBuffer ) ) != RING_BUFFER_RESULT_OK )
        {
            LogError( ( "Failed to initialize ring buffer!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        pCtx->protocols[ 0 ].name = "https";
        pCtx->protocols[ 0 ].callback = LwsHttpCallback;
        pCtx->protocols[ 0 ].user = &( pCtx->httpContext );
        pCtx->protocols[ 1 ].name = "wss";
        pCtx->protocols[ 1 ].callback = LwsWebsocketCallback;
        pCtx->protocols[ 1 ].user = &( pCtx->websocketContext );
        pCtx->protocols[ 2 ].callback = NULL; /* End marker. */

        memset( &( creationInfo ), 0, sizeof( struct lws_context_creation_info ) );
        creationInfo.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        creationInfo.port = CONTEXT_PORT_NO_LISTEN;
        creationInfo.protocols = &( pCtx->protocols[ 0 ] );
        creationInfo.timeout_secs = 10;
        creationInfo.gid = -1;
        creationInfo.uid = -1;
        creationInfo.client_ssl_ca_filepath = pCreds->pCaCertPath;
        creationInfo.client_ssl_cipher_list = "HIGH:!PSK:!RSP:!eNULL:!aNULL:!RC4:!MD5:!DES:!3DES:!aDH:!kDH:!DSS";
        creationInfo.ka_time = 1;
        creationInfo.ka_probes = 1;
        creationInfo.ka_interval = 1;
        creationInfo.retry_and_idle_policy = &( retryPolicy );

        if( ( pCreds->pDeviceCertPath != NULL ) &&( pCreds->pDeviceKeyPath != NULL ) )
        {
            creationInfo.client_ssl_cert_filepath = pCreds->pDeviceCertPath;
            creationInfo.client_ssl_private_key_filepath = pCreds->pDeviceKeyPath;
        }

        lws_set_log_level( LLL_NOTICE | LLL_WARN | LLL_ERR, NULL );
        pCtx->pLwsContext = lws_create_context( &( creationInfo ) );

        if( pCtx->pLwsContext == NULL )
        {
            LogError( ( "lws_create_context failed!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

NetworkingResult_t Networking_HttpSend( NetworkingContext_t * pCtx,
                                        HttpRequest_t * pRequest,
                                        const AwsCredentials_t * pAwsCredentials,
                                        HttpResponse_t * pResponse )
{
    NetworkingResult_t ret = NETWORKING_RESULT_OK;
    char * pHost, * pPath;
    size_t hostLength, pathLength;
    struct lws_client_connect_info connectInfo;
    struct lws * clientLws;

    if( ( pCtx == NULL ) ||
        ( pRequest == NULL ) ||
        ( pResponse == NULL ) )
    {
        ret = NETWORKING_RESULT_BAD_PARAM;
    }

    /* Fill up required headers. */
    if( ret == NETWORKING_RESULT_OK )
    {
        pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_USER_AGENT_IDX ].pName = "user-agent";
        pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_USER_AGENT_IDX ].pValue = pRequest->pUserAgent;
        pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_USER_AGENT_IDX ].valueLength = pRequest->userAgentLength;

        if( GetHostFromUrl( pRequest->pUrl,
                            pRequest->urlLength,
                            &( pHost ),
                            &( hostLength ) ) == 0 )
        {
            if( hostLength <= URI_HOST_BUFFER_LENGTH )
            {
                memcpy( &( pCtx->uriHost[ 0 ] ),
                        pHost,
                        hostLength );
                pCtx->uriHost[ hostLength ] = '\0';
                pCtx->uriHostLength = hostLength;
            }
            else
            {
                LogError( ( "uriHost buffer is not large enough to fit the host, hostLength = %lu!", hostLength ) );
                ret = NETWORKING_RESULT_FAIL;
            }
        }
        else
        {
            LogError( ( "Failed to extract host from the URL!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        pCtx->iso8601TimeLength = ISO8601_TIME_LENGTH;

        if( GetCurrentTimeInIso8601Format( &( pCtx->iso8601Time[ 0 ] ),
                                           &( pCtx->iso8601TimeLength ) ) == 0 )
        {
            pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_ISO8601_TIME_IDX ].pName = "x-amz-date";
            pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_ISO8601_TIME_IDX ].pValue = &( pCtx->iso8601Time[ 0 ] );
            pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_ISO8601_TIME_IDX ].valueLength = pCtx->iso8601TimeLength;
        }
        else
        {
            LogError( ( "Failed to get ISO8601 time!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        if( ( pAwsCredentials != NULL ) &&
            ( pAwsCredentials->pSessionToken != NULL ) &&
            ( pAwsCredentials->sessionTokenLength > 0 ) )
        {
            pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_SESSION_TOKEN_IDX ].pName = "x-amz-security-token";
            pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_SESSION_TOKEN_IDX ].pValue = pAwsCredentials->pSessionToken;
            pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_SESSION_TOKEN_IDX ].valueLength = pAwsCredentials->sessionTokenLength;
        }
        else
        {
            pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_SESSION_TOKEN_IDX ].pValue = NULL;
            pCtx->httpContext.requiredHeaders[ REQUIRED_HEADER_SESSION_TOKEN_IDX ].valueLength = 0;
        }
    }

    /* Extract the path section from URL. */
    if( ret == NETWORKING_RESULT_OK )
    {
        if( GetPathFromUrl( pRequest->pUrl,
                            pRequest->urlLength,
                            &( pPath ),
                            &( pathLength ) ) == 0 )
        {
            if( pathLength <= URI_PATH_BUFFER_LENGTH )
            {
                memcpy( &( pCtx->uriPath[ 0 ] ),
                        pPath,
                        pathLength );
                pCtx->uriPath[ 0 ] = '/';
                pCtx->uriPath[ pathLength ] = '\0';
                pCtx->uriPathLength = pathLength;
            }
            else
            {
                LogError( ( "uriPath buffer is not large enough to fit the path, pathLength = %lu!", pathLength ) );
                ret = NETWORKING_RESULT_FAIL;
            }
        }
        else
        {
            LogError( ( "Failed to extract path from the URL!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    /* Sign the request if needed. */
    if( ( ret == NETWORKING_RESULT_OK ) &&
        ( pAwsCredentials != NULL ) )
    {
        if( SignHttpRequest( pCtx,
                             pRequest,
                             pAwsCredentials ) != 0 )
        {
            LogError( ( "Failed to sign HTTP request!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    /* Send the request and wait for the response. */
    if( ret == NETWORKING_RESULT_OK )
    {
        /* Needed to append optional headers and write body in the callback. */
        pCtx->httpContext.pRequest = pRequest;

        /* Needed to receive the response in the user supplied buffer. */
        pCtx->httpContext.pResponse = pResponse;

        memset( &( connectInfo ), 0, sizeof( struct lws_client_connect_info ) );

        connectInfo.context = pCtx->pLwsContext;
        connectInfo.ssl_connection = LCCSCF_USE_SSL;
        connectInfo.port = 443;
        connectInfo.address = &( pCtx->uriHost[ 0 ] );
        connectInfo.path = &( pCtx->uriPath[ 0 ] );
        connectInfo.host = connectInfo.address;
        connectInfo.pwsi = &( clientLws );
        connectInfo.opaque_user_data = NULL;
        connectInfo.method = pRequest->verb == HTTP_GET ? "GET" : "POST";
        connectInfo.protocol = "https";

        ( void ) lws_client_connect_via_info( &( connectInfo ) );

        pCtx->httpContext.connectionClosed = 0U;
        while( pCtx->httpContext.connectionClosed == 0U )
        {
            ( void ) lws_service( pCtx->pLwsContext, 0 );
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

NetworkingResult_t Networking_WebsocketConnect( NetworkingContext_t * pCtx,
                                                const WebsocketConnectInfo_t * pConnectInfo,
                                                const AwsCredentials_t * pAwsCredentials )
{
    NetworkingResult_t ret = NETWORKING_RESULT_OK;
    struct lws_client_connect_info connectInfo;
    struct lws * clientLws;
    char * pHost;
    size_t hostLength = 0;

    if( ( pCtx == NULL ) ||
        ( pConnectInfo == NULL ) ||
        ( pAwsCredentials == NULL ) )
    {
        ret = NETWORKING_RESULT_BAD_PARAM;
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        if( GetHostFromUrl( pConnectInfo->pUrl,
                            pConnectInfo->urlLength,
                            &( pHost ),
                            &( hostLength ) ) == 0 )
        {
            if( hostLength <= URI_HOST_BUFFER_LENGTH )
            {
                memcpy( &( pCtx->uriHost[ 0 ] ),
                        pHost,
                        hostLength );
                pCtx->uriHost[ hostLength ] = '\0';
                pCtx->uriHostLength = hostLength;
            }
            else
            {
                LogError( ( "uriHost buffer is not large enough to fit the host, hostLength = %lu!", hostLength ) );
                ret = NETWORKING_RESULT_FAIL;
            }
        }
        else
        {
            LogError( ( "Failed to extract host from the URL!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        pCtx->iso8601TimeLength = ISO8601_TIME_LENGTH;

        if( GetCurrentTimeInIso8601Format( &( pCtx->iso8601Time[ 0 ] ),
                                           &( pCtx->iso8601TimeLength ) ) != 0 )
        {
            LogError( ( "Failed to get ISO8601 time!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        if( SignWebsocketRequest( pCtx,
                                  pConnectInfo,
                                  pAwsCredentials ) != 0 )
        {
            LogError( ( "Failed to sign Websocket request!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    /* Send the request and wait for the response. */
    if( ret == NETWORKING_RESULT_OK )
    {
        pCtx->websocketContext.rxCallback = pConnectInfo->rxCallback;
        pCtx->websocketContext.pRxCallbackData = pConnectInfo->pRxCallbackData;

        memset( &( connectInfo ), 0, sizeof( struct lws_client_connect_info ) );

        connectInfo.context = pCtx->pLwsContext;
        connectInfo.ssl_connection = LCCSCF_USE_SSL;
        connectInfo.port = 443;
        connectInfo.address = &( pCtx->uriHost[ 0 ] );
        connectInfo.path = &( pCtx->uriPath[ 0 ] );
        connectInfo.host = connectInfo.address;
        connectInfo.pwsi = &( clientLws );
        connectInfo.opaque_user_data = NULL;
        connectInfo.method = NULL;
        connectInfo.protocol = "wss";

        pCtx->websocketContext.pWsi = lws_client_connect_via_info( &( connectInfo ) );

        pCtx->websocketContext.connectionEstablished = 0U;
        pCtx->websocketContext.connectionClosed = 0U;
        while( ( pCtx->websocketContext.connectionEstablished == 0U ) &&
               ( pCtx->websocketContext.connectionClosed == 0U ) )
        {
            ( void ) lws_service( pCtx->pLwsContext, 0 );
        }

        if( pCtx->websocketContext.connectionClosed == 1U )
        {
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/

NetworkingResult_t Networking_WebsocketSend( NetworkingContext_t * pCtx,
                                             const char * pMessage,
                                             size_t messageLength )
{
    NetworkingResult_t ret = NETWORKING_RESULT_OK;
    char * pWebsocketMessage;

    if( ( pCtx == NULL ) ||
        ( pMessage == NULL ) ||
        ( messageLength == 0 ) )
    {
        ret = NETWORKING_RESULT_BAD_PARAM;
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        pWebsocketMessage = ( char * ) malloc( LWS_PRE + messageLength );

        if( pWebsocketMessage == NULL )
        {
            LogError( ( "Failed to allocate buffer for Websocket message!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        memcpy( &( pWebsocketMessage[ LWS_PRE ] ),
                pMessage,
                messageLength );

        if( RingBuffer_Insert( &( pCtx->websocketContext.ringBuffer ),
                               pWebsocketMessage,
                               messageLength ) != RING_BUFFER_RESULT_OK )
        {
            ret = NETWORKING_RESULT_FAIL;
            free( pWebsocketMessage );
            LogError( ( "Failed to insert Websocket message to ring buffer!" ) );
        }
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        /* This will cause a LWS_CALLBACK_EVENT_WAIT_CANCELLED in the lws
         * service thread context. */
        lws_cancel_service( pCtx->pLwsContext );

    }

    return ret;
}

/*----------------------------------------------------------------------------*/

NetworkingResult_t Networking_WebsocketSignal( NetworkingContext_t * pCtx )
{
    NetworkingResult_t ret = NETWORKING_RESULT_OK;

    if( pCtx == NULL )
    {
        ret = NETWORKING_RESULT_BAD_PARAM;
    }

    if( ret == NETWORKING_RESULT_OK )
    {
        lws_service( pCtx->pLwsContext, 0 );

        if( pCtx->websocketContext.connectionClosed == 1 )
        {
            LogWarn( ( "Websocket connection is closed!" ) );
            ret = NETWORKING_RESULT_FAIL;
        }
    }

    return ret;
}

/*----------------------------------------------------------------------------*/
