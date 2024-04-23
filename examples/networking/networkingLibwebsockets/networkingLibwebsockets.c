#include <string.h>
#include <time.h>
#include "logging.h"
#include "networkingLibwebsockets.h"
#include "networkingLibwebsockets_private.h"
#include "libwebsockets.h"
#include "openssl/sha.h"

#define NETWORKING_LWS_STRING_SCHEMA_DELIMITER "://"
#define NETWORKING_LWS_STRING_HTTPS "https"
#define NETWORKING_LWS_STRING_HTTPS_METHOD "POST"
#define NETWORKING_LWS_STRING_WSS "wss"
#define NETWORKING_LWS_STRING_WSS_METHOD "GET"
#define NETWORKING_LWS_URI_ENCODED_CHAR_SIZE ( 3 ) // We need 3 char spaces to translate symbols, such as from '/' to "%2F".
#define NETWORKING_LWS_URI_ENCODED_FORWARD_SLASH "%2F"

static int32_t sha256Init( void * hashContext );
static int32_t sha256Update( void * hashContext,
                             const uint8_t * pInput,
                             size_t inputLen );
static int32_t sha256Final( void * hashContext,
                            uint8_t * pOutput,
                            size_t outputLen );

static struct lws_protocols protocols[ NETWORKING_LWS_PROTOCOLS_NUM + 1 ];
NetworkingLibwebsocketContext_t networkingLibwebsocketContext;

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

NetworkingLibwebsocketsResult_t initRingBuffer( NetworkingLibwebsocketRingBuffer_t *pRingBuffer )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;

    pRingBuffer->start = 0;
    pRingBuffer->end = 0;

    return ret;
}

NetworkingLibwebsocketsResult_t getRingBufferCurrentIndex( NetworkingLibwebsocketRingBuffer_t *pRingBuffer, size_t *pCurrentIdx )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    uint8_t isEmpty = pRingBuffer->end == pRingBuffer->start? 1:0;

    if( isEmpty )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_EMPTY;
    }
    else
    {
        *pCurrentIdx = pRingBuffer->start;
    }

    return ret;
}

NetworkingLibwebsocketsResult_t allocateRingBuffer( NetworkingLibwebsocketRingBuffer_t *pRingBuffer, size_t *pNextIdx )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t nextIndex = ( pRingBuffer->end + 1 ) % NETWORKING_LWS_RING_BUFFER_NUM;
    uint8_t isFull = nextIndex == pRingBuffer->start? 1:0;

    if( isFull )
    {
        LogWarn( ( "Ring buffer is now full" ) );
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_FULL;
    }
    else
    {
        *pNextIdx = pRingBuffer->end;
        pRingBuffer->end = nextIndex;
    }

    return ret;
}

NetworkingLibwebsocketsResult_t freeRingBuffer( NetworkingLibwebsocketRingBuffer_t *pRingBuffer, size_t idx )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t nextIndex = ( pRingBuffer->start + 1 ) % NETWORKING_LWS_RING_BUFFER_NUM;
    uint8_t isCorrectIdx = ( idx == pRingBuffer->start )? 1:0;

    if( !isCorrectIdx )
    {
        LogError( ( "Freeing incorrect index: %ld, expected index: %ld", idx, pRingBuffer->start ) );
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_RING_BUFFER_FREE_WRONG_INDEX;
    }
    else if( pRingBuffer->start == pRingBuffer->end )
    {
        /* Ring buffer is now empty, not necessary to free. */
    }
    else
    {
        pRingBuffer->start = nextIndex;
        pRingBuffer->bufferInfo[ idx ].bufferLength = 0;
        pRingBuffer->bufferInfo[ idx ].offset = 0;
    }

    return ret;
}

NetworkingLibwebsocketsResult_t uriEncodedString( char *pSrc, size_t srcLength, char *pDst, size_t *pDstLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    size_t encodedLength = 0, remainLength;
    char *pCurPtr = pSrc, *pEnc = pDst;
    char ch;
    const char alpha[17] = "0123456789ABCDEF";

    if( pSrc == NULL || pDst == NULL || pDstLength == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        // Set the remainLength length
        remainLength = *pDstLength;

        while( ( ( size_t ) ( pCurPtr - pSrc ) < srcLength ) && ( ( ch = *pCurPtr++ ) != '\0') )
        {
            if( ( ch >= 'A' && ch <= 'Z' ) || ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '_' || ch == '-' || ch == '~' || ch == '.')
            {
                if( remainLength < 1U )
                {
                    ret = NETWORKING_LIBWEBSOCKETS_RESULT_URI_ENCODED_BUFFER_TOO_SMALL;
                    break;
                }
                
                encodedLength++;
                *pEnc++ = ch;
                remainLength--;
            }
            else if( ch == '/' )
            {
                if( remainLength < NETWORKING_LWS_URI_ENCODED_CHAR_SIZE )
                {
                    ret = NETWORKING_LIBWEBSOCKETS_RESULT_URI_ENCODED_BUFFER_TOO_SMALL;
                    break;
                }
                
                encodedLength += NETWORKING_LWS_URI_ENCODED_CHAR_SIZE;
                strncpy( pEnc, NETWORKING_LWS_URI_ENCODED_FORWARD_SLASH, remainLength );
                pEnc += NETWORKING_LWS_URI_ENCODED_CHAR_SIZE;
                remainLength -= NETWORKING_LWS_URI_ENCODED_CHAR_SIZE;
            }
            else
            {
                if( remainLength < NETWORKING_LWS_URI_ENCODED_CHAR_SIZE )
                {
                    ret = NETWORKING_LIBWEBSOCKETS_RESULT_URI_ENCODED_BUFFER_TOO_SMALL;
                    break;
                }
                
                encodedLength += NETWORKING_LWS_URI_ENCODED_CHAR_SIZE;
                *pEnc++ = '%';
                *pEnc++ = alpha[ch >> 4];
                *pEnc++ = alpha[ch & 0x0f];
                remainLength -= NETWORKING_LWS_URI_ENCODED_CHAR_SIZE;
            }
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        *pDstLength -= remainLength;
    }

    return ret;
}

NetworkingLibwebsocketsResult_t generateAuthorizationHeader( NetworkingLibwebsocketCanonicalRequest_t *pCanonicalRequest )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    SigV4HttpParameters_t sigv4HttpParams;
    SigV4Status_t sigv4Status = SigV4Success;
    uint8_t isHttp;

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        if( pCanonicalRequest->verb == NETWORKING_LWS_HTTP_VERB_GET )
        {
            /* We only use GET method in websocket. */
            isHttp = 0U;
        }
        else if( pCanonicalRequest->verb == NETWORKING_LWS_HTTP_VERB_POST )
        {
            /* We only use POST method in HTTP. */
            isHttp = 1U;
        }
        else
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_INVALID_AUTH_VERB;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        /* Setup the HTTP parameters for SigV4. */
        sigv4HttpParams.flags = 0;
        if( isHttp )
        {
            sigv4HttpParams.pHttpMethod = NETWORKING_LWS_STRING_HTTPS_METHOD;
            sigv4HttpParams.httpMethodLen = strlen( NETWORKING_LWS_STRING_HTTPS_METHOD );
        }
        else
        {
            sigv4HttpParams.pHttpMethod = NETWORKING_LWS_STRING_WSS_METHOD;
            sigv4HttpParams.httpMethodLen = strlen( NETWORKING_LWS_STRING_WSS_METHOD );
            sigv4HttpParams.flags |= SIGV4_HTTP_QUERY_IS_CANONICAL_FLAG;
        }
        sigv4HttpParams.pPath = pCanonicalRequest->pPath;
        sigv4HttpParams.pathLen = pCanonicalRequest->pathLength;
        sigv4HttpParams.pQuery = pCanonicalRequest->pCanonicalQueryString;
        sigv4HttpParams.queryLen = pCanonicalRequest->canonicalQueryStringLength;
        sigv4HttpParams.pHeaders = pCanonicalRequest->pCanonicalHeaders;
        sigv4HttpParams.headersLen = pCanonicalRequest->canonicalHeadersLength;
        sigv4HttpParams.pPayload = pCanonicalRequest->pPayload;
        sigv4HttpParams.payloadLen = pCanonicalRequest->payloadLength;
        
        /* Initializing sigv4Params with Http parameters required for the HTTP request. */
        sigv4Params.pHttpParameters = &sigv4HttpParams;
        sigv4Params.pRegion = networkingLibwebsocketContext.libwebsocketsCredentials.pRegion;
        sigv4Params.regionLen = networkingLibwebsocketContext.libwebsocketsCredentials.regionLength;
        sigv4Params.pCredentials = &networkingLibwebsocketContext.sigv4Credential;
        sigv4Params.pDateIso8601 = networkingLibwebsocketContext.appendHeaders.pDate;
        cryptoInterface.pHashContext = &networkingLibwebsocketContext.sha256Ctx;

        /* Reset buffer length then generate authorization. */
        networkingLibwebsocketContext.sigv4AuthLen = NETWORKING_LWS_SIGV4_AUTH_BUFFER_LENGTH;
        sigv4Status = SigV4_GenerateHTTPAuthorization( &sigv4Params, networkingLibwebsocketContext.sigv4AuthBuffer, &networkingLibwebsocketContext.sigv4AuthLen,
                                                       &networkingLibwebsocketContext.appendHeaders.pSignature, &networkingLibwebsocketContext.appendHeaders.signatureLength );
        
        if( sigv4Status != SigV4Success )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SIGV4_GENERATE_AUTH_FAIL;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        networkingLibwebsocketContext.appendHeaders.pAuthorization = networkingLibwebsocketContext.sigv4AuthBuffer;
        networkingLibwebsocketContext.appendHeaders.authorizationLength = networkingLibwebsocketContext.sigv4AuthLen;
    }

    return ret;
}

NetworkingLibwebsocketsResult_t performLwsConnect( char *pHost, size_t hostLength, uint16_t port, uint8_t isHttp )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    struct lws_client_connect_info connectInfo;
    struct lws* clientLws;
    int32_t lwsReturn;
    static char host[ NETWORKING_LWS_URI_HOST_MAX_LENGTH + 1 ];

    if( hostLength > NETWORKING_LWS_URI_HOST_MAX_LENGTH )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_HOST_BUFFER_TOO_SMALL;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        memcpy( host, pHost, hostLength );
        host[ hostLength ] = '\0';

        memset( &connectInfo, 0, sizeof( struct lws_client_connect_info ) );
        connectInfo.context = networkingLibwebsocketContext.pLwsContext;
        connectInfo.ssl_connection = LCCSCF_USE_SSL;
        connectInfo.port = port;
        connectInfo.address = host;
        connectInfo.path = networkingLibwebsocketContext.pathBuffer;
        connectInfo.host = connectInfo.address;
        connectInfo.pwsi = &clientLws;
        connectInfo.opaque_user_data = NULL;

        if( isHttp )
        {
            connectInfo.method = NETWORKING_LWS_STRING_HTTPS_METHOD;
            connectInfo.protocol = NETWORKING_LWS_STRING_HTTPS;
            networkingLibwebsocketContext.pLws[ NETWORKING_LWS_PROTOCOLS_HTTP_INDEX ] = lws_client_connect_via_info( &connectInfo );
        }
        else
        {
            connectInfo.method = NULL;
            connectInfo.protocol = NETWORKING_LWS_STRING_WSS;
            networkingLibwebsocketContext.pLws[ NETWORKING_LWS_PROTOCOLS_WEBSOCKET_INDEX ] = lws_client_connect_via_info( &connectInfo );
        }

        networkingLibwebsocketContext.terminateLwsService = 0U;
        while( networkingLibwebsocketContext.terminateLwsService == 0U )
        {
            lwsReturn = lws_service(networkingLibwebsocketContext.pLwsContext, 0);
        }
    }

    return ret;
}

NetworkingLibwebsocketsResult_t performLwsRecv()
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    int32_t lwsReturn;

    lwsReturn = lws_service(networkingLibwebsocketContext.pLwsContext, 0);

    return ret;
}

NetworkingLibwebsocketsResult_t getPathFromUrl( char *pUrl, size_t urlLength, char **ppPath, size_t *pPathLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char *pHost, *pPathEnd;
    size_t hostLength;
    char *pStart;

    if( pUrl == NULL || ppPath == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        /* Get host pointer & length */
        ret = getUrlHost( pUrl, urlLength, &pHost, &hostLength );
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        /* Find '?' as end of path if any query parameters. */
        pStart = strchr( pHost + hostLength, '?' );

        if( pStart == NULL )
        {
            /* All the remaining part after the host belongs to the path. */
            pPathEnd = pUrl + urlLength;
            *ppPath = pHost + hostLength;
            *pPathLength = pPathEnd - *ppPath;
        }
        else
        {
            /* The part after the host until '?' belongs to the path. */
            pPathEnd = pStart;
            *ppPath = pHost + hostLength;
            *pPathLength = pPathEnd - *ppPath;
        }
    }

    return ret;
}

NetworkingLibwebsocketsResult_t getIso8601CurrentTime( char **ppDate, size_t * pDateLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
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

NetworkingLibwebsocketsResult_t getUrlHost( char *pUrl, size_t urlLength, char **ppStart, size_t *pHostLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char *pStart = NULL, *pEnd = pUrl + urlLength, *pCurPtr;
    uint8_t foundEndMark = 0;

    if( pUrl == NULL || ppStart == NULL || pHostLength == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        // Start from the schema delimiter
        pStart = strstr(pUrl, NETWORKING_LWS_STRING_SCHEMA_DELIMITER);
        if( pStart == NULL )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SCHEMA_DELIMITER_NOT_FOUND;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        // Advance the pStart past the delimiter
        pStart += strlen(NETWORKING_LWS_STRING_SCHEMA_DELIMITER);

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

void NetworkingLibwebsockets_Signal( struct lws_context *pLwsContext )
{
    lws_cancel_service( pLwsContext );
}

NetworkingLibwebsocketsResult_t NetworkingLibwebsockets_Init( NetworkingLibwebsocketsCredentials_t * pCredential )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    static uint8_t first = 0U;
    struct lws_context_creation_info creationInfo;
    const lws_retry_bo_t retryPolicy = {
        .secs_since_valid_ping = 10,
        .secs_since_valid_hangup = 7200,
    };

    if( pCredential == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK && !first )
    {
        memcpy( &networkingLibwebsocketContext.libwebsocketsCredentials, pCredential, sizeof(NetworkingLibwebsocketsCredentials_t) );
        networkingLibwebsocketContext.sigv4Credential.pAccessKeyId = pCredential->pAccessKeyId;
        networkingLibwebsocketContext.sigv4Credential.accessKeyIdLen = pCredential->accessKeyIdLength;
        networkingLibwebsocketContext.sigv4Credential.pSecretAccessKey = pCredential->pSecretAccessKey;
        networkingLibwebsocketContext.sigv4Credential.secretAccessKeyLen = pCredential->secretAccessKeyLength;

        if( networkingLibwebsocketContext.libwebsocketsCredentials.userAgentLength > NETWORKING_LWS_USER_AGENT_NAME_MAX_LENGTH )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_USER_AGENT_NAME_TOO_LONG;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK && !first )
    {
        protocols[ NETWORKING_LWS_PROTOCOLS_HTTP_INDEX ].name = NETWORKING_LWS_STRING_HTTPS;
        protocols[ NETWORKING_LWS_PROTOCOLS_HTTP_INDEX ].callback = lwsHttpCallbackRoutine;
        protocols[ NETWORKING_LWS_PROTOCOLS_WEBSOCKET_INDEX ].name = NETWORKING_LWS_STRING_WSS;
        protocols[ NETWORKING_LWS_PROTOCOLS_WEBSOCKET_INDEX ].callback = lwsWebsocketCallbackRoutine;

        memset(&creationInfo, 0, sizeof(struct lws_context_creation_info));
        creationInfo.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        creationInfo.port = CONTEXT_PORT_NO_LISTEN;
        creationInfo.protocols = protocols;
        creationInfo.timeout_secs = 10;
        creationInfo.gid = -1;
        creationInfo.uid = -1;
        creationInfo.client_ssl_ca_filepath = networkingLibwebsocketContext.libwebsocketsCredentials.pCaCertPath;
        creationInfo.client_ssl_cipher_list = "HIGH:!PSK:!RSP:!eNULL:!aNULL:!RC4:!MD5:!DES:!3DES:!aDH:!kDH:!DSS";
        creationInfo.ka_time = 1;
        creationInfo.ka_probes = 1;
        creationInfo.ka_interval = 1;
        creationInfo.retry_and_idle_policy = &retryPolicy;

        lws_set_log_level(LLL_NOTICE | LLL_WARN | LLL_ERR, NULL);
        networkingLibwebsocketContext.pLwsContext = lws_create_context( &creationInfo );

        if( networkingLibwebsocketContext.pLwsContext == NULL )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_INIT_LWS_CONTEXT_FAIL;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK && !first )
    {
        /* Initialize Tx ring buffer. */
        ret = initRingBuffer( &networkingLibwebsocketContext.websocketTxRingBuffer );
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK && !first )
    {
        first = 1U;
    }

    return ret;
}
