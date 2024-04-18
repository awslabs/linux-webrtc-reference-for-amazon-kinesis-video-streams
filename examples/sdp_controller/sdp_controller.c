#include <stdlib.h>
#include "logging.h"
#include "sdp_controller.h"
#include "core_json.h"
#include "sdp_deserializer.h"

#define SDP_CONTROLLER_SDP_OFFER_MESSAGE_TYPE_KEY "type"
#define SDP_CONTROLLER_SDP_OFFER_MESSAGE_TYPE_VALUE "offer"
#define SDP_CONTROLLER_SDP_OFFER_MESSAGE_CONTENT_KEY "sdp"
#define SDP_CONTROLLER_SDP_NEWLINE_ENDING "\\n"

static SdpControllerResult_t convertStringToUl( const char *pStr, size_t strLength, uint32_t *pOutUl )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    uint32_t i, result = 0;

    if( pStr == NULL || pOutUl == NULL )
    {
        return SDP_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        for( i = 0; pStr[i] != '\0' && i < strLength; i++ )
        { 
            if( pStr[i] >= '0' && pStr[i] <= '9' )
            {
                result = result * 10 + ( pStr[i] - '0' );
            }
            else
            {
                ret = SDP_CONTROLLER_RESULT_SDP_NON_NUMBERIC_STRING;
                break;
            }
        } 
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        *pOutUl = result;
    }
    
    return ret;
}

static SdpControllerResult_t parseMediaAttributes( SdpControllerSdpOffer_t *pOffer, const char *pAttributeBuffer, size_t attributeBufferLength )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpAttribute_t attribute;
    uint8_t mediaIndex = pOffer->mediaCount - 1;
    uint8_t *pAttributeCount = &pOffer->mediaDescriptions[ mediaIndex ].mediaAttributesCount;

    if( pOffer->mediaDescriptions[ mediaIndex ].mediaAttributesCount >= SDP_CONTROLLER_MAX_SDP_ATTRIBUTES_COUNT )
    {
        ret = SDP_CONTROLLER_RESULT_SDP_MEDIA_ATTRIBUTE_MAX_EXCEDDED;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        sdpResult = SdpDeserializer_ParseAttribute( pAttributeBuffer, attributeBufferLength, &attribute );
        if( sdpResult != SDP_RESULT_OK )
        {
            ret = SDP_CONTROLLER_RESULT_SDP_DESERIALIZER_PARSE_ATTRIBUTE_FAIL;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        pOffer->mediaDescriptions[ mediaIndex ].attributes[ *pAttributeCount ].pAttributeName = attribute.pAttributeName;
        pOffer->mediaDescriptions[ mediaIndex ].attributes[ *pAttributeCount ].attributeNameLength = attribute.attributeNameLength;
        pOffer->mediaDescriptions[ mediaIndex ].attributes[ *pAttributeCount ].pAttributeValue = attribute.pAttributeValue;
        pOffer->mediaDescriptions[ mediaIndex ].attributes[ *pAttributeCount ].attributeValueLength = attribute.attributeValueLength;
        (*pAttributeCount)++;
    }

    return ret;
}

static SdpControllerResult_t parseSessionAttributes( SdpControllerSdpOffer_t *pOffer, const char *pAttributeBuffer, size_t attributeBufferLength )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpAttribute_t attribute;

    if( pOffer->sessionAttributesCount >= SDP_CONTROLLER_MAX_SDP_ATTRIBUTES_COUNT )
    {
        ret = SDP_CONTROLLER_RESULT_SDP_SESSION_ATTRIBUTE_MAX_EXCEDDED;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        sdpResult = SdpDeserializer_ParseAttribute( pAttributeBuffer, attributeBufferLength, &attribute );
        if( sdpResult != SDP_RESULT_OK )
        {
            ret = SDP_CONTROLLER_RESULT_SDP_DESERIALIZER_PARSE_ATTRIBUTE_FAIL;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        pOffer->attributes[ pOffer->sessionAttributesCount ].pAttributeName = attribute.pAttributeName;
        pOffer->attributes[ pOffer->sessionAttributesCount ].attributeNameLength = attribute.attributeNameLength;
        pOffer->attributes[ pOffer->sessionAttributesCount ].pAttributeValue = attribute.pAttributeValue;
        pOffer->attributes[ pOffer->sessionAttributesCount ].attributeValueLength = attribute.attributeValueLength;
        pOffer->sessionAttributesCount++;
    }

    return ret;
}

SdpControllerResult_t SdpController_DeserializeSdpOffer( const char *pSdpOfferContent, size_t sdpOfferContentLength, SdpControllerSdpOffer_t *pOffer )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpDeserializerContext_t ctx;
    const char *pValue;
    char *pEnd;
    size_t valueLength;
    uint8_t type;

    if( pSdpOfferContent == NULL || pOffer == NULL )
    {
        ret = SDP_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        memset( pOffer, 0, sizeof( SdpControllerSdpOffer_t ) );

        sdpResult = SdpDeserializer_Init( &ctx, pSdpOfferContent, sdpOfferContentLength );
        if( sdpResult != SDP_RESULT_OK )
        {
            LogError( ( "Init SDP deserializer failure, result: %d", sdpResult ) );
            ret = SDP_CONTROLLER_RESULT_SDP_DESERIALIZER_INIT_FAIL;
        }
    }

    while( sdpResult == SDP_RESULT_OK )
    {
        sdpResult = SdpDeserializer_GetNext( &ctx, &type, &pValue, &valueLength );

        if( sdpResult != SDP_RESULT_OK )
        {
            break;
        }
        else if( type == SDP_TYPE_MEDIA )
        {
            pOffer->mediaDescriptions[ pOffer->mediaCount ].pMediaName = pValue;
            pOffer->mediaDescriptions[ pOffer->mediaCount ].mediaNameLength = valueLength;
            pOffer->mediaCount++;
        }
        else if( pOffer->mediaCount != 0)
        {
            if( type == SDP_TYPE_ATTRIBUTE )
            {
                ret = parseMediaAttributes( pOffer, pValue, valueLength );
                if( ret != SDP_CONTROLLER_RESULT_OK )
                {
                    LogError( ( "parseMediaAttributes fail, result %d", ret ) );
                    break;
                }
            }
            else if( type == SDP_TYPE_SESSION_INFO )
            {
                // Media Title
                pOffer->mediaDescriptions[ pOffer->mediaCount - 1 ].pMediaTitle = pValue;
                pOffer->mediaDescriptions[ pOffer->mediaCount - 1 ].mediaTitleLength = valueLength;
            }
            else
            {
                /* Do nothing. */
            }
        }
        else
        {
            /* No media description before, these attributes belongs to session. */
            if( type == SDP_TYPE_SESSION_NAME )
            {
                // SDP Session Name
                pOffer->pSessionName = pValue;
                pOffer->sessionNameLength = valueLength;
            }
            else if( type == SDP_TYPE_SESSION_INFO )
            {
                // SDP Session Information
                pOffer->pSessionInformation = pValue;
                pOffer->sessionInformationLength = valueLength;
            }
            else if( type == SDP_TYPE_URI )
            {
                // SDP URI
                pOffer->pUri = pValue;
                pOffer->uriLength = valueLength;
            }
            else if( type == SDP_TYPE_EMAIL )
            {
                // SDP Email Address
                pOffer->pEmailAddress = pValue;
                pOffer->emailAddressLength = valueLength;
            }
            else if( type == SDP_TYPE_PHONE )
            {
                // SDP Phone number
                pOffer->pPhoneNumber = pValue;
                pOffer->phoneNumberLength = valueLength;
            }
            else if( type == SDP_TYPE_VERSION )
            {
                // Version
                ret = convertStringToUl( pValue, valueLength, &pOffer->version );
                if( ret != SDP_CONTROLLER_RESULT_OK )
                {
                    LogError( ( "convertStringToUl fail, result %d, converting %.*s to %u",
                                ret,
                                ( int ) valueLength, pValue,
                                pOffer->version ) );
                    break;
                }
            }
            else if( type == SDP_TYPE_ATTRIBUTE )
            {
                ret = parseSessionAttributes( pOffer, pValue, valueLength );
                if( ret != SDP_CONTROLLER_RESULT_OK )
                {
                    LogError( ( "parseSessionAttributes fail, result %d", ret ) );
                    break;
                }
            } else
            {
                /* Do nothing. */
            }
        }
    }

    return ret;
}

SdpControllerResult_t SdpController_GetSdpOfferContent( const char *pSdpMessage, size_t sdpMessageLength, const char **ppSdpOfferContent, size_t *pSdpOfferContentLength )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    JSONStatus_t jsonResult;
    size_t start = 0, next = 0;
    JSONPair_t pair = { 0 };
    uint8_t isContentFound = 0;

    if( pSdpMessage == NULL || ppSdpOfferContent == NULL || pSdpOfferContentLength == NULL )
    {
        ret = SDP_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        jsonResult = JSON_Validate( pSdpMessage, sdpMessageLength );

        if( jsonResult != JSONSuccess)
        {
            ret = SDP_CONTROLLER_RESULT_INVALID_JSON;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        /* Check if it's SDP offer. */
        jsonResult = JSON_Iterate( pSdpMessage, sdpMessageLength, &start, &next, &pair );

        while( jsonResult == JSONSuccess )
        {
            if( strncmp( pair.key, SDP_CONTROLLER_SDP_OFFER_MESSAGE_TYPE_KEY, pair.keyLength ) == 0 &&
                strncmp( pair.value, SDP_CONTROLLER_SDP_OFFER_MESSAGE_TYPE_VALUE, pair.valueLength ) != 0 )
            {
                /* It's not expected SDP offer message. */
                LogWarn( ( "Message type \"%.*s\" is not SDP offer type\n", 
                           ( int ) pair.valueLength, pair.value ) );
                ret = SDP_CONTROLLER_RESULT_NOT_SDP_OFFER;
            }
            else if( strncmp( pair.key, SDP_CONTROLLER_SDP_OFFER_MESSAGE_CONTENT_KEY, pair.keyLength ) == 0 )
            {
                *ppSdpOfferContent = pair.value;
                *pSdpOfferContentLength = pair.valueLength;
                isContentFound = 1;
                break;
            }
            else
            {
                /* Skip unknown attributes. */
            }

            jsonResult = JSON_Iterate( pSdpMessage, sdpMessageLength, &start, &next, &pair );
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK && !isContentFound )
    {
        ret = SDP_CONTROLLER_RESULT_NOT_SDP_OFFER;
    }

    return ret;
}

SdpControllerResult_t SdpController_ConvertSdpContentNewline( const char *pSdpContent, size_t sdpContentLength, char **ppSdpConvertedContent, size_t *pSdpConvertedContentLength )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    const char *pCurSdp = pSdpContent, *pNext;
    char *pCurOutput = *ppSdpConvertedContent;
    size_t lineLength, outputLength = 0;

    if( pSdpContent == NULL || ppSdpConvertedContent == NULL || pSdpConvertedContentLength == NULL )
    {
        ret = SDP_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        while( ( pNext = strstr( pCurSdp, SDP_CONTROLLER_SDP_NEWLINE_ENDING ) ) != NULL )
        {
            lineLength = pNext - pCurSdp;

            if( lineLength >= 2 &&
                pCurSdp[ lineLength - 2 ] == '\\' && pCurSdp[ lineLength - 1 ] == 'r' )
            {
                lineLength -= 2;
            }

            if( *pSdpConvertedContentLength < outputLength + lineLength + 2 )
            {
                ret = SDP_CONTROLLER_RESULT_SDP_CONVERTED_BUFFER_TOO_SMALL;
                break;
            }

            memcpy( pCurOutput, pCurSdp, lineLength );
            pCurOutput += lineLength;
            *pCurOutput++ = '\r';
            *pCurOutput++ = '\n';
            outputLength += lineLength + 2;

            pCurSdp = pNext + 2;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        *pSdpConvertedContentLength = outputLength;
    }

    return ret;
}
