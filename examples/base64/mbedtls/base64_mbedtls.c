#include "logging.h"
#include "base64.h"
#include "mbedtls/base64.h"

static Base64Result_t Base64_InterpretReturnValue( int errorCode )
{
    Base64Result_t ret = BASE64_RESULT_OK;

    switch( errorCode )
    {
        case MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL:
            ret = BASE64_RESULT_BUFFER_TOO_SMALL;
            break;

        case MBEDTLS_ERR_BASE64_INVALID_CHARACTER:
        default:
            ret = BASE64_RESULT_INVALID_INPUT;
            break;
    }

    return ret;
}

Base64Result_t Base64_Encode( const char * pInputData,
                              size_t inputDataLength,
                              char * pOutputData,
                              size_t * pOutputDataLength )
{
    Base64Result_t ret = BASE64_RESULT_OK;
    size_t olen = 0;
    int retBase64;

    retBase64 = mbedtls_base64_encode( ( unsigned char * ) pOutputData, *pOutputDataLength, &olen, ( const unsigned char * ) pInputData, inputDataLength );

    if( retBase64 == 0 )
    {
        /* Update output length for user. */
        *pOutputDataLength = olen;
    }
    else
    {
        ret = Base64_InterpretReturnValue( retBase64 );
    }

    return ret;
}

Base64Result_t Base64_Decode( const char * pInputData,
                              size_t inputDataLength,
                              char * pOutputData,
                              size_t * pOutputDataLength )
{
    Base64Result_t ret = BASE64_RESULT_OK;
    size_t olen = 0;
    int retBase64;

    retBase64 = mbedtls_base64_decode( ( unsigned char * ) pOutputData, *pOutputDataLength, &olen, ( const unsigned char * ) pInputData, inputDataLength );

    if( retBase64 == 0 )
    {
        /* Update output length for user. */
        *pOutputDataLength = olen;
    }
    else
    {
        ret = Base64_InterpretReturnValue( retBase64 );
    }

    return ret;
}
