#include <stdlib.h>
#include "logging.h"
#include "sdp_controller.h"
#include "core_json.h"
#include "sdp_deserializer.h"
#include "sdp_serializer.h"

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

static SdpControllerResult_t parseMediaAttributes( SdpControllerSdpDescription_t *pOffer, const char *pAttributeBuffer, size_t attributeBufferLength )
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

static SdpControllerResult_t parseSessionAttributes( SdpControllerSdpDescription_t *pOffer, const char *pAttributeBuffer, size_t attributeBufferLength )
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

static SdpControllerResult_t serializeOrigin( SdpSerializerContext_t *pCtx, SdpControllerOrigin_t *pOrigin )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpOriginator_t origin;

    memset( &origin, 0, sizeof( SdpOriginator_t ) );
    origin.sessionId = pOrigin->sessionId;
    origin.sessionVersion = pOrigin->sessionVersion;
    origin.pUserName = pOrigin->pUserName;
    origin.userNameLength = pOrigin->userNameLength;
    origin.connectionInfo.networkType = SDP_NETWORK_IN;
    origin.connectionInfo.addressType = SDP_ADDRESS_IPV4;
    origin.connectionInfo.pAddress = pOrigin->sdpConnectionInformation.pConnectionAddress;
    origin.connectionInfo.addressLength = pOrigin->sdpConnectionInformation.connectionAddressLength;

    sdpResult = SdpSerializer_AddOriginator( pCtx, SDP_TYPE_ORIGINATOR, &origin );
    if( sdpResult != SDP_RESULT_OK )
    {
        LogError( ( "Serialize SDP origin failure, result: %d", sdpResult ) );
        ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
    }

    return ret;
}

static SdpControllerResult_t serializeTiming( SdpSerializerContext_t *pCtx, SdpControllerTiming_t *pTiming )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpTimeDescription_t time;

    memset( &time, 0, sizeof( SdpTimeDescription_t ) );
    time.startTime = pTiming->startTime;
    time.stopTime = pTiming->stopTime;

    sdpResult = SdpSerializer_AddTimeActive( pCtx, SDP_TYPE_TIME_ACTIVE, &time );
    if( sdpResult != SDP_RESULT_OK )
    {
        LogError( ( "Serialize SDP time active failure, result: %d", sdpResult ) );
        ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
    }

    return ret;
}

static SdpControllerResult_t serializeAttributes( SdpSerializerContext_t *pCtx, SdpControllerAttributes_t *pAttributes, uint16_t attributeCount )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpAttribute_t attribute;
    uint32_t i;
    SdpControllerAttributes_t *pCurrentAttrubute = pAttributes;

    for( i=0 ; i<attributeCount ; i++ )
    {
        attribute.pAttributeName = (pCurrentAttrubute + i)->pAttributeName;
        attribute.attributeNameLength = (pCurrentAttrubute + i)->attributeNameLength;
        attribute.pAttributeValue = (pCurrentAttrubute + i)->pAttributeValue;
        attribute.attributeValueLength = (pCurrentAttrubute + i)->attributeValueLength;

        sdpResult = SdpSerializer_AddAttribute( pCtx, SDP_TYPE_ATTRIBUTE, &attribute );
        if( sdpResult != SDP_RESULT_OK )
        {
            LogError( ( "Serialize SDP attribute failure, result: %d", sdpResult ) );
            ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
        }
    }

    return ret;
}

static SdpControllerResult_t serializeConnectionInfo( SdpSerializerContext_t *pCtx, SdpControllerConnectionInformation_t *pConnectionInfo )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpConnectionInfo_t connInfo;

    connInfo.networkType = SDP_NETWORK_IN;
    connInfo.addressType = SDP_ADDRESS_IPV4;
    connInfo.pAddress = pConnectionInfo->pConnectionAddress;
    connInfo.addressLength = pConnectionInfo->connectionAddressLength;

    sdpResult = SdpSerializer_AddConnectionInfo( pCtx, SDP_TYPE_CONNINFO, &connInfo );
    if( sdpResult != SDP_RESULT_OK )
    {
        LogError( ( "Serialize SDP connection information failure, result: %d", sdpResult ) );
        ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
    }

    return ret;
}

static SdpControllerResult_t serializeMedias( SdpSerializerContext_t *pCtx, SdpControllerMediaDescription_t *pMediaDescriptions, uint16_t mediaCount )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpAttribute_t attribute;
    uint32_t i;
    SdpControllerMediaDescription_t *pCurrentMedia = pMediaDescriptions;

    for( i=0 ; i<mediaCount ; i++ )
    {
        pCurrentMedia = pMediaDescriptions + i;

        /* Media name */
        sdpResult = SdpSerializer_AddBuffer( pCtx, SDP_TYPE_MEDIA, pCurrentMedia->pMediaName, pCurrentMedia->mediaNameLength );
        if( sdpResult != SDP_RESULT_OK )
        {
            LogError( ( "Serialize SDP media name failure, result: %d", sdpResult ) );
            ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
            break;
        }

        /* Media title */
        if( pCurrentMedia->pMediaTitle )
        {
            sdpResult = SdpSerializer_AddBuffer( pCtx, SDP_TYPE_MEDIA, pCurrentMedia->pMediaTitle, pCurrentMedia->mediaTitleLength );
            if( sdpResult != SDP_RESULT_OK )
            {
                LogError( ( "Serialize SDP media title failure, result: %d", sdpResult ) );
                ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
                break;
            }
        }

        /* Media connection information */
        ret = serializeConnectionInfo( pCtx, &pCurrentMedia->connectionInformation );
        if( ret != SDP_CONTROLLER_RESULT_OK )
        {
            break;
        }

        /* Append media attributes. */
        ret = serializeAttributes( pCtx, &pCurrentMedia->attributes[ 0 ], pCurrentMedia->mediaAttributesCount );
        if( ret != SDP_CONTROLLER_RESULT_OK )
        {
            break;
        }
    }

    return ret;
}

static SdpControllerResult_t serializeSdpMessage( SdpControllerSdpDescription_t *pSdpDescription, char *pOutputBuffer, size_t *pOutputBufferSize )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    SdpResult_t sdpResult = SDP_RESULT_OK;
    SdpSerializerContext_t ctx;
    const char *pBuffer;

    sdpResult = SdpSerializer_Init( &ctx, pOutputBuffer, *pOutputBufferSize );
    if( sdpResult != SDP_RESULT_OK )
    {
        LogError( ( "Init SDP serializer failure, result: %d", sdpResult ) );
        ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_INIT_FAIL;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        /* Append version. */
        sdpResult = SdpSerializer_AddU64( &ctx, SDP_TYPE_VERSION, pSdpDescription->version );
        if( sdpResult != SDP_RESULT_OK )
        {
            LogError( ( "Serialize SDP version failure, result: %d", sdpResult ) );
            ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        /* Append origin. */
        ret = serializeOrigin( &ctx, &pSdpDescription->origin );
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        /* Append session name. */
        sdpResult = SdpSerializer_AddBuffer( &ctx, SDP_TYPE_SESSION_NAME, pSdpDescription->pSessionName, pSdpDescription->sessionNameLength );
        if( sdpResult != SDP_RESULT_OK )
        {
            LogError( ( "Serialize SDP session name failure, result: %d", sdpResult ) );
            ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        /* Append timing description. */
        ret = serializeTiming( &ctx, &pSdpDescription->timingDescription );
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        /* Append session attributes. */
        ret = serializeAttributes( &ctx, &pSdpDescription->attributes[ 0 ], pSdpDescription->sessionAttributesCount );
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        /* Append media information. */
        ret = serializeMedias( &ctx, &pSdpDescription->mediaDescriptions[ 0 ], pSdpDescription->mediaCount );
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        sdpResult = SdpSerializer_Finalize( &ctx, &pBuffer, pOutputBufferSize );
        if( sdpResult != SDP_RESULT_OK )
        {
            LogError( ( "Serialize SDP finalize failure, result: %d", sdpResult ) );
            ret = SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL;
        }
    }

    return ret;
}

SdpControllerResult_t SdpController_DeserializeSdpOffer( const char *pSdpOfferContent, size_t sdpOfferContentLength, SdpControllerSdpDescription_t *pOffer )
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
        memset( pOffer, 0, sizeof( SdpControllerSdpDescription_t ) );

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

SdpControllerResult_t SdpController_DeserializeSdpContentNewline( const char *pSdpContent, size_t sdpContentLength, char **ppSdpConvertedContent, size_t *pSdpConvertedContentLength )
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

SdpControllerResult_t SdpController_SerializeSdpNewline( const char *pSdpContent, size_t sdpContentLength, char *pSdpConvertedContent, size_t *pSdpConvertedContentLength )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    const char *pCurSdp = pSdpContent, *pNext, *pTail;
    char *pCurOutput = pSdpConvertedContent;
    size_t lineLength, outputLength = 0;
    int writtenLength;

    if( pSdpContent == NULL || pSdpConvertedContent == NULL || pSdpConvertedContentLength == NULL )
    {
        ret = SDP_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        pTail = pSdpContent + sdpContentLength;

        while( ( pNext = memchr( pCurSdp, '\n', pTail - pCurSdp ) ) != NULL )
        {
            lineLength = pNext - pCurSdp;

            if( lineLength > 0 &&
                pCurSdp[ lineLength - 1 ] == '\r' )
            {
                lineLength--;
            }
            else
            {
                /* do nothing, coverity happy. */
            }

            if( *pSdpConvertedContentLength < outputLength + lineLength + 4 )
            {
                ret = SDP_CONTROLLER_RESULT_SDP_CONVERTED_BUFFER_TOO_SMALL;
                break;
            }

            writtenLength = snprintf( pCurOutput, *pSdpConvertedContentLength - outputLength, "%.*s\\r\\n",
                                      ( int ) lineLength,
                                      pCurSdp );
            if( writtenLength < 0 )
            {
                ret = SDP_CONTROLLER_RESULT_SDP_SNPRINTF_FAIL;
                LogError( ( "snprintf returns fail %d", writtenLength ) );
                break;
            }
            else
            {
                outputLength += lineLength + 4;
                pCurOutput += lineLength + 4;
            }

            pCurSdp = pNext + 1;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        if( pTail > pCurSdp )
        {
            /* Copy the ending string. */
            lineLength = pTail - pCurSdp;
            memcpy( pCurOutput, pCurSdp, lineLength );

            outputLength += lineLength;
            pCurOutput += lineLength;
            pCurSdp += lineLength;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        *pSdpConvertedContentLength = outputLength;
    }

    return ret;
}

SdpControllerResult_t SdpController_SerializeSdpMessage( SdpControllerMessageType_t messageType, SdpControllerSdpDescription_t *pSdpDescription, char *pSdpMessage, size_t *pSdpMessageLength )
{
    SdpControllerResult_t ret = SDP_CONTROLLER_RESULT_OK;
    int written;
    char *pCurrentOutput = pSdpMessage;
    size_t outputBufferWrittenSize = 0U, remainSize;

    if( pSdpDescription == NULL || pSdpMessage == NULL || pSdpMessageLength == NULL )
    {
        ret = SDP_CONTROLLER_RESULT_BAD_PARAMETER;
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        remainSize = *pSdpMessageLength - outputBufferWrittenSize;
        written = snprintf( pCurrentOutput, remainSize, SDP_CONTROLLER_MESSAGE_TEMPLATE_HEAD, messageType==SDP_CONTROLLER_MESSAGE_TYPE_OFFER? "offer":"answer" );

        if( written < 0 )
        {
            LogError( ( "Unexpected behavior, snprintf returns %d", written ) );
            ret = SDP_CONTROLLER_RESULT_SDP_SNPRINTF_FAIL;
        }
        else if( written == remainSize )
        {
            LogError( ( "output buffer full" ) );
            ret = SDP_CONTROLLER_RESULT_SDP_CONVERTED_BUFFER_TOO_SMALL;
        }
        else
        {
            pCurrentOutput += written;
            outputBufferWrittenSize += written;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        remainSize = *pSdpMessageLength - outputBufferWrittenSize;
        ret = serializeSdpMessage( pSdpDescription, pCurrentOutput, &remainSize );
        if( ret == SDP_CONTROLLER_RESULT_OK )
        {
            /* remainSize is updated to written length in serializeSdpMessage. */
            pCurrentOutput += remainSize;
            outputBufferWrittenSize += remainSize;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        remainSize = *pSdpMessageLength - outputBufferWrittenSize;
        written = snprintf( pCurrentOutput, remainSize, SDP_CONTROLLER_MESSAGE_TEMPLATE_TAIL );

        if( written < 0 )
        {
            LogError( ( "Unexpected behavior, snprintf returns %d", written ) );
            ret = SDP_CONTROLLER_RESULT_SDP_SNPRINTF_FAIL;
        }
        else if( written == remainSize )
        {
            LogError( ( "output buffer full" ) );
            ret = SDP_CONTROLLER_RESULT_SDP_CONVERTED_BUFFER_TOO_SMALL;
        }
        else
        {
            pCurrentOutput += written;
            outputBufferWrittenSize += written;
        }
    }

    if( ret == SDP_CONTROLLER_RESULT_OK )
    {
        /* Update written length for user. */
        *pSdpMessageLength = outputBufferWrittenSize;
    }

    return ret;
}
