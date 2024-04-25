#include "string_utils.h"

StringUtilsResult_t StringUtils_ConvertStringToUl( const char *pStr, size_t strLength, uint32_t *pOutUl )
{
    StringUtilsResult_t ret = STRING_UTILS_RESULT_OK;
    uint32_t i, result = 0;

    if( pStr == NULL || pOutUl == NULL )
    {
        return STRING_UTILS_RESULT_OK;
    }

    if( ret == STRING_UTILS_RESULT_OK )
    {
        for( i = 0; pStr[i] != '\0' && i < strLength; i++ )
        { 
            if( pStr[i] >= '0' && pStr[i] <= '9' )
            {
                result = result * 10 + ( pStr[i] - '0' );
            }
            else
            {
                ret = STRING_UTILS_RESULT_NON_NUMBERIC_STRING;
                break;
            }
        } 
    }

    if( ret == STRING_UTILS_RESULT_OK )
    {
        *pOutUl = result;
    }
    
    return ret;
}