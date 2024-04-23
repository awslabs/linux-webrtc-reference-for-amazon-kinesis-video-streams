#ifndef SDP_CONTROLLER_H
#define SDP_CONTROLLER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

#define SDP_CONTROLLER_MAX_SDP_SESSION_TIMING_COUNT ( 2 )
#define SDP_CONTROLLER_MAX_SDP_SESSION_TIMEZONE_COUNT ( 2 )
#define SDP_CONTROLLER_MAX_SDP_ATTRIBUTES_COUNT ( 256 )
#define SDP_CONTROLLER_MAX_SDP_MEDIA_DESCRIPTIONS_COUNT ( 5 )

#define SDP_CONTROLLER_ORIGIN_DEFAULT_USER_NAME "-"
#define SDP_CONTROLLER_ORIGIN_DEFAULT_SESSION_VERSION ( 2 )
#define SDP_CONTROLLER_ORIGIN_DEFAULT_NET_TYPE "IN"
#define SDP_CONTROLLER_ORIGIN_IPV4_TYPE "IP4"
#define SDP_CONTROLLER_ORIGIN_DEFAULT_IP_ADDRESS "127.0.0.1"

#define SDP_CONTROLLER_ORIGIN_DEFAULT_SESSION_NAME "-"

#define SDP_CONTROLLER_MESSAGE_TEMPLATE_HEAD "{\"type\": \"%s\", \"sdp\": \""
#define SDP_CONTROLLER_MESSAGE_TEMPLATE_TAIL "\"}"

typedef enum SdpControllerResult
{
    SDP_CONTROLLER_RESULT_OK = 0,
    SDP_CONTROLLER_RESULT_BAD_PARAMETER,
    SDP_CONTROLLER_RESULT_INVALID_JSON,
    SDP_CONTROLLER_RESULT_NOT_SDP_OFFER,
    SDP_CONTROLLER_RESULT_SDP_DESERIALIZER_INIT_FAIL,
    SDP_CONTROLLER_RESULT_SDP_DESERIALIZER_PARSE_ATTRIBUTE_FAIL,
    SDP_CONTROLLER_RESULT_SDP_SERIALIZER_INIT_FAIL,
    SDP_CONTROLLER_RESULT_SDP_SERIALIZER_ADD_FAIL,
    SDP_CONTROLLER_RESULT_SDP_SESSION_ATTRIBUTE_MAX_EXCEDDED,
    SDP_CONTROLLER_RESULT_SDP_MEDIA_ATTRIBUTE_MAX_EXCEDDED,
    SDP_CONTROLLER_RESULT_SDP_NON_NUMBERIC_STRING,
    SDP_CONTROLLER_RESULT_SDP_CONVERTED_BUFFER_TOO_SMALL,
    SDP_CONTROLLER_RESULT_SDP_SNPRINTF_FAIL,
} SdpControllerResult_t;

/*
 * c=<nettype> <addrtype> <connection-address>
 * https://tools.ietf.org/html/rfc4566#section-5.7
 */
typedef struct SdpControllerConnectionInformation
{
    const char *pNetworkType;
    size_t networkTypeLength;
    const char *pAddressType;
    size_t addressTypeLength;
    const char *pConnectionAddress;
    size_t connectionAddressLength;
} SdpControllerConnectionInformation_t;

/*
 * o=<username> <sess-id> <sess-version> <nettype> <addrtype> <unicast-address>
 * https://tools.ietf.org/html/rfc4566#section-5.2
 */
typedef struct SdpControllerOrigin
{
    const char *pUserName;
    size_t userNameLength;
    uint64_t sessionId;
    uint64_t sessionVersion;
    SdpControllerConnectionInformation_t sdpConnectionInformation;
} SdpControllerOrigin_t;

/*
 * https://tools.ietf.org/html/rfc4566#section-5.9
 * https://tools.ietf.org/html/rfc4566#section-5.10
 */
typedef struct SdpControllerTiming
{
    uint64_t startTime;
    uint64_t stopTime;
} SdpControllerTiming_t;

/*
 * z=<adjustment time> <offset> <adjustment time> <offset> ...
 * https://tools.ietf.org/html/rfc4566#section-5.11
 */
typedef struct SdpControllerTimeZone
{
    uint64_t adjustmentTime;
    const char *pOffset;
    size_t offsetLength;
} SdpControllerTimeZone_t;

/*
 * a=<attribute>
 * a=<attribute>:<value>
 * https://tools.ietf.org/html/rfc4566#section-5.13
 */
typedef struct SdpControllerAttributes
{
    const char *pAttributeName;
    size_t attributeNameLength;
    const char *pAttributeValue;
    size_t attributeValueLength;
} SdpControllerAttributes_t;

typedef struct SdpControllerMediaDescription
{
    // m=<media> <port>/<number of ports> <proto> <fmt> ...
    // https://tools.ietf.org/html/rfc4566#section-5.14
    const const char *pMediaName;
    size_t mediaNameLength;

    // i=<session description>
    // https://tools.ietf.org/html/rfc4566#section-5.4. Given these are free-form strings, the length could be anything.
    // Although our SDK parses this information, the SDK does not use it. Leaving this attribute in if SDK uses it in
    // the future
    const const char *pMediaTitle;
    size_t mediaTitleLength;

    SdpControllerConnectionInformation_t connectionInformation;

    SdpControllerAttributes_t attributes[ SDP_CONTROLLER_MAX_SDP_ATTRIBUTES_COUNT ];

    uint8_t mediaAttributesCount;
} SdpControllerMediaDescription_t;

typedef struct SdpControllerSdpOffer
{
    // https://tools.ietf.org/html/rfc4566#section-5.1
    uint32_t version;

    SdpControllerOrigin_t origin;

    // s=<session name>
    // https://tools.ietf.org/html/rfc4566#section-5.3
    const char *pSessionName;
    size_t sessionNameLength;

    // i=<session description>
    // https://tools.ietf.org/html/rfc4566#section-5.4
    const char *pSessionInformation;
    size_t sessionInformationLength;

    // u=<uri>
    // https://tools.ietf.org/html/rfc4566#section-5.5
    const char *pUri;
    size_t uriLength;

    // e=<email-address>
    // https://tools.ietf.org/html/rfc4566#section-5.6
    const char *pEmailAddress;
    size_t emailAddressLength;

    // p=<phone-number>
    // https://tools.ietf.org/html/rfc4566#section-5.6
    const char *pPhoneNumber;
    size_t phoneNumberLength;

    SdpControllerConnectionInformation_t connectionInformation;

    SdpControllerTiming_t timingDescription;

    SdpControllerAttributes_t attributes[ SDP_CONTROLLER_MAX_SDP_ATTRIBUTES_COUNT ];

    SdpControllerMediaDescription_t mediaDescriptions[ SDP_CONTROLLER_MAX_SDP_MEDIA_DESCRIPTIONS_COUNT ];

    uint16_t sessionAttributesCount;

    uint16_t mediaCount;
} SdpControllerSdpDescription_t;

typedef enum SdpControllerMessageType
{
    SDP_CONTROLLER_MESSAGE_TYPE_NONE = 0,
    SDP_CONTROLLER_MESSAGE_TYPE_OFFER,
    SDP_CONTROLLER_MESSAGE_TYPE_ANSWER,
} SdpControllerMessageType_t;

SdpControllerResult_t SdpController_GetSdpOfferContent( const char *pSdpMessage, size_t sdpMessageLength, const char **ppSdpOfferContent, size_t *pSdpOfferContentLength );
SdpControllerResult_t SdpController_DeserializeSdpContentNewline( const char *pSdpContent, size_t sdpContentLength, char **ppSdpConvertedContent, size_t *pSdpConvertedContentLength );
SdpControllerResult_t SdpController_DeserializeSdpOffer( const char *pSdpOfferContent, size_t sdpOfferContentLength, SdpControllerSdpDescription_t *pOffer );
SdpControllerResult_t SdpController_SerializeSdpMessage( SdpControllerMessageType_t messageType, SdpControllerSdpDescription_t *pSdpDescription, char *pSdpMessage, size_t *pSdpMessageLength );
SdpControllerResult_t SdpController_SerializeSdpNewline( const char *pSdpContent, size_t sdpContentLength, char *pSdpConvertedContent, size_t *pSdpConvertedContentLength );

#ifdef __cplusplus
}
#endif

#endif /* SDP_CONTROLLER_H */