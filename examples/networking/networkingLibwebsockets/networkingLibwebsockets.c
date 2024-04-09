#include <string.h>
#include <time.h>
#include "networkingLibwebsockets.h"
#include "networkingLibwebsockets_private.h"
#include "libwebsockets.h"
#include "openssl/sha.h"

#define NETWORKING_LWS_STRING_SCHEMA_DELIMITER_STRING "://"

static int32_t sha256Init( void * hashContext );
static int32_t sha256Update( void * hashContext,
                             const uint8_t * pInput,
                             size_t inputLen );
static int32_t sha256Final( void * hashContext,
                            uint8_t * pOutput,
                            size_t outputLen );

static struct lws_protocols protocols[3];
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

NetworkingLibwebsocketsResult_t getPathFromUrl( char *pUrl, size_t urlLength, char **ppPath, size_t *pPathLength )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    char *pHost;
    size_t hostLength;

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
        /* Calculate remaining length of URL after host. */
        *ppPath = pHost + hostLength;
        *pPathLength = urlLength - ( urlLength - hostLength );
    }

    return ret;
}

NetworkingLibwebsocketsResult_t generateAuthorizationHeader( NetworkingLibwebsocketContext_t *pCtx )
{
    NetworkingLibwebsocketsResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    SigV4HttpParameters_t sigv4HttpParams;
    SigV4Status_t sigv4Status = SigV4Success;
    /* Store Signature used in AWS HTTP requests generated using SigV4 library. */
    char * signature = NULL;
    size_t signatureLen = 0;
    char headersBuffer[ 4096 ];
    size_t remainSize = sizeof( headersBuffer );
    int32_t snprintfReturn;
    NetworkingLibwebsocketsAppendHeaders_t *pAppendHeaders = &pCtx->appendHeaders;

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
        sigv4HttpParams.pPath = pCtx->pathBuffer;
        sigv4HttpParams.pathLen = pCtx->pathBufferWrittenLength;
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

        /* Reset buffer length then generate authorization. */
        pCtx->sigv4AuthLen = NETWORKING_SIGV4_AUTH_BUFFER_LENGTH;
        sigv4Status = SigV4_GenerateHTTPAuthorization( &sigv4Params, pCtx->sigv4AuthBuffer, &pCtx->sigv4AuthLen, &signature, &signatureLen );
        
        if( sigv4Status != SigV4Success )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_SIGV4_GENERATE_AUTH_FAIL;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK )
    {
        pCtx->appendHeaders.pAuthorization = pCtx->sigv4AuthBuffer;
        pCtx->appendHeaders.authorizationLength = pCtx->sigv4AuthLen;
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

NetworkingLibwebsocketsResult_t NetworkingLibwebsockets_Init( NetworkingLibwebsocketContext_t *pCtx, NetworkingLibwebsocketsCredentials_t * pCredential )
{
    HttpResult_t ret = NETWORKING_LIBWEBSOCKETS_RESULT_OK;
    static uint8_t first = 0U;
    struct lws_context_creation_info creationInfo;
    const lws_retry_bo_t retryPolicy = {
        .secs_since_valid_ping = 10,
        .secs_since_valid_hangup = 7200,
    };

    if( pCtx == NULL || pCredential == NULL )
    {
        ret = NETWORKING_LIBWEBSOCKETS_RESULT_BAD_PARAMETER;
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK && !first )
    {
        memcpy( &pCtx->libwebsocketsCredentials, pCredential, sizeof(NetworkingLibwebsocketsCredentials_t) );
        pCtx->sigv4Credential.pAccessKeyId = pCredential->pAccessKeyId;
        pCtx->sigv4Credential.accessKeyIdLen = pCredential->accessKeyIdLength;
        pCtx->sigv4Credential.pSecretAccessKey = pCredential->pSecretAccessKey;
        pCtx->sigv4Credential.secretAccessKeyLen = pCredential->secretAccessKeyLength;

        if( pCtx->libwebsocketsCredentials.userAgentLength > NETWORKING_LWS_USER_AGENT_NAME_MAX_LENGTH )
        {
            ret = NETWORKING_LIBWEBSOCKETS_RESULT_USER_AGENT_NAME_TOO_LONG;
        }
    }

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK && !first )
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

    if( ret == NETWORKING_LIBWEBSOCKETS_RESULT_OK && !first )
    {
        first = 1U;
    }

    return ret;
}
