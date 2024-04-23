#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "demo_config.h"
#include "logging.h"
#include "demo_data_types.h"
#include "signaling_controller.h"
#include "sdp_controller.h"

DemoContext_t demoContext;
SignalingControllerContext_t signalingControllerContext;

const char audioAttribute0Name[] = "candidate";
const char audioAttribute0Value[] = "0 1 udp 2130706431 172.26.66.7 58246 typ host raddr 0.0.0.0 rport 0 generation 0 network-cost 999";
const char audioAttribute1Name[] = "msid";
const char audioAttribute1Value[] = "myKvsVideoStream myAudioTrack";
const char audioAttribute2Name[] = "ssrc";
const char audioAttribute2Value[] = "1427941850 cname:fPXvBF4p7CVUy8HQ";
const char audioAttribute3Name[] = "ssrc";
const char audioAttribute3Value[] = "1427941850 msid:myKvsVideoStream myAudioTrack";
const char audioAttribute4Name[] = "ssrc";
const char audioAttribute4Value[] = "1427941850 mslabel:myKvsVideoStream";
const char audioAttribute5Name[] = "ssrc";
const char audioAttribute5Value[] = "1427941850 label:myAudioTrack";
const char audioAttribute6Name[] = "rtcp";
const char audioAttribute6Value[] = "9 IN IP4 0.0.0.0";
const char audioAttribute7Name[] = "ice-ufrag";
const char audioAttribute7Value[] = "GnjB";
const char audioAttribute8Name[] = "ice-pwd";
const char audioAttribute8Value[] = "eu8hMmfpUkEU3t1DfJb+/J3e";
const char audioAttribute9Name[] = "ice-options";
const char audioAttribute9Value[] = "trickle";
const char audioAttribute10Name[] = "fingerprint";
const char audioAttribute10Value[] = "sha-256 27:22:01:EC:8F:37:2B:9D:1D:6C:77:A7:8F:9B:19:8F:1B:C1:AA:56:33:2F:1A:19:30:85:06:C7:7D:84:FD:2A";
const char audioAttribute11Name[] = "setup";
const char audioAttribute11Value[] = "active";
const char audioAttribute12Name[] = "mid";
const char audioAttribute12Value[] = "0";
const char audioAttribute13Name[] = "sendrecv";
const char audioAttribute14Name[] = "rtcp-mux";
const char audioAttribute15Name[] = "rtpmap";
const char audioAttribute15Value[] = "111 opus/48000/2";
const char audioAttribute16Name[] = "fmtp";
const char audioAttribute16Value[] = "111 minptime=10;useinbandfec=1";
const char audioAttribute17Name[] = "rtcp-fb";
const char audioAttribute17Value[] = "111 goog-remb";
const char audioAttribute18Name[] = "rtcp-fb";
const char audioAttribute18Value[] = "111 transport-cc";
const char videoAttribute0Name[] = "candidate";
const char videoAttribute0Value[] = "0 1 udp 2130706431 172.26.66.7 58246 typ host raddr 0.0.0.0 rport 0 generation 0 network-cost 999";
const char videoAttribute1Name[] = "msid";
const char videoAttribute1Value[] = "myKvsVideoStream myVideoTrack";
const char videoAttribute2Name[] = "ssrc";
const char videoAttribute2Value[] = "351461057 cname:fPXvBF4p7CVUy8HQ";
const char videoAttribute3Name[] = "ssrc";
const char videoAttribute3Value[] = "351461057 msid:myKvsVideoStream myVideoTrack";
const char videoAttribute4Name[] = "ssrc";
const char videoAttribute4Value[] = "351461057 mslabel:myKvsVideoStream";
const char videoAttribute5Name[] = "ssrc";
const char videoAttribute5Value[] = "351461057 label:myVideoTrack";
const char videoAttribute6Name[] = "rtcp";
const char videoAttribute6Value[] = "9 IN IP4 0.0.0.0";
const char videoAttribute7Name[] = "ice-ufrag";
const char videoAttribute7Value[] = "GnjB";
const char videoAttribute8Name[] = "ice-pwd";
const char videoAttribute8Value[] = "eu8hMmfpUkEU3t1DfJb+/J3e";
const char videoAttribute9Name[] = "ice-options";
const char videoAttribute9Value[] = "trickle";
const char videoAttribute10Name[] = "fingerprint";
const char videoAttribute10Value[] = "sha-256 27:22:01:EC:8F:37:2B:9D:1D:6C:77:A7:8F:9B:19:8F:1B:C1:AA:56:33:2F:1A:19:30:85:06:C7:7D:84:FD:2A";
const char videoAttribute11Name[] = "setup";
const char videoAttribute11Value[] = "active";
const char videoAttribute12Name[] = "mid";
const char videoAttribute12Value[] = "1";
const char videoAttribute13Name[] = "sendrecv";
const char videoAttribute14Name[] = "rtcp-mux";
const char videoAttribute15Name[] = "rtcp-rsize";
const char videoAttribute16Name[] = "rtpmap";
const char videoAttribute16Value[] = "106 H264/90000";
const char videoAttribute17Name[] = "rtcp-fb";
const char videoAttribute17Value[] = "106 nack pli";
const char videoAttribute18Name[] = "fmtp";
const char videoAttribute18Value[] = "106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f";
const char videoAttribute19Name[] = "rtcp-fb";
const char videoAttribute19Value[] = "106 goog-remb";
const char videoAttribute20Name[] = "rtcp-fb";
const char videoAttribute20Value[] = "106 transport-cc";

static void terminateHandler( int sig )
{
    SignalingController_Deinit( &signalingControllerContext );
    exit( 0 );
}

static uint8_t storeAndParseSdpOffer( const char *pEventSdpOffer, size_t eventSdpOfferlength, DemoSessionInformation_t *pSessionInDescriptionOffer )
{
    uint8_t skipProcess = 0;
    SdpControllerResult_t retSdpController;
    const char *pSdpContent;
    size_t sdpContentLength;
    char *pSdpBuffer = pSessionInDescriptionOffer->sdpBuffer;
    size_t *pSdpBufferLength = &pSessionInDescriptionOffer->sdpBufferLength;

    /* Store the SDP offer then parse it in poiters structure. */
    retSdpController = SdpController_GetSdpOfferContent( pEventSdpOffer, eventSdpOfferlength, &pSdpContent, &sdpContentLength );
    if( retSdpController != SDP_CONTROLLER_RESULT_OK )
    {
        LogError( ( "Unable to find SDP offer content, result: %d", retSdpController ) );
        skipProcess = 1;
    }

    if( !skipProcess )
    {
        if( sdpContentLength > DEMO_SDP_BUFFER_MAX_LENGTH )
        {
            LogError( ( "No enough memory to store SDP offer" ) );
            skipProcess = 1;
        }
        else
        {
            /* Keep SDP concent in global buffer after replacing newline, then we can keep accessing the parsed result in pointers structure. */
            pSessionInDescriptionOffer->sdpBufferLength = DEMO_SDP_BUFFER_MAX_LENGTH;
            retSdpController = SdpController_DeserializeSdpContentNewline( pSdpContent, sdpContentLength,
                                                                           &pSdpBuffer, pSdpBufferLength );
            if( retSdpController != SDP_CONTROLLER_RESULT_OK )
            {
                skipProcess = 1;
                LogError( ( "Unable to convert SDP offer, result: %d", retSdpController ) );
            }
        }
    }

    if( !skipProcess )
    {
        retSdpController = SdpController_DeserializeSdpOffer( pSessionInDescriptionOffer->sdpBuffer,
                                                              pSessionInDescriptionOffer->sdpBufferLength,
                                                              &pSessionInDescriptionOffer->sdpDescription );
        if( retSdpController != SDP_CONTROLLER_RESULT_OK )
        {
            skipProcess = 1;
            LogError( ( "Unable to deserialize SDP offer, result: %d", retSdpController ) );
        }
    }

    return skipProcess;
}

static void populateSessionOrigin( char **ppBuffer, size_t *pBufferLength, SdpControllerOrigin_t *pOrigin )
{
    pOrigin->pUserName = SDP_CONTROLLER_ORIGIN_DEFAULT_USER_NAME;
    pOrigin->userNameLength = strlen( SDP_CONTROLLER_ORIGIN_DEFAULT_USER_NAME );
    pOrigin->sessionId = rand();
    pOrigin->sessionVersion = SDP_CONTROLLER_ORIGIN_DEFAULT_SESSION_VERSION;
    pOrigin->sdpConnectionInformation.pNetworkType = SDP_CONTROLLER_ORIGIN_DEFAULT_NET_TYPE;
    pOrigin->sdpConnectionInformation.networkTypeLength = strlen( SDP_CONTROLLER_ORIGIN_DEFAULT_NET_TYPE );
    pOrigin->sdpConnectionInformation.pAddressType = SDP_CONTROLLER_ORIGIN_IPV4_TYPE;
    pOrigin->sdpConnectionInformation.addressTypeLength = strlen( SDP_CONTROLLER_ORIGIN_IPV4_TYPE );
    pOrigin->sdpConnectionInformation.pConnectionAddress = SDP_CONTROLLER_ORIGIN_DEFAULT_IP_ADDRESS;
    pOrigin->sdpConnectionInformation.connectionAddressLength = strlen( SDP_CONTROLLER_ORIGIN_DEFAULT_IP_ADDRESS );
}

static uint8_t populateSessionAttributes( char **ppBuffer, size_t *pBufferLength, SdpControllerSdpDescription_t *pSdpDescription )
{
    uint8_t skipProcess = 0;
    int written;
    size_t remainSize = *pBufferLength;
    char *pCurBuffer = *ppBuffer;

    /* group. */
    written = snprintf( pCurBuffer, remainSize, "group" );
    if( written < 0 )
    {
        skipProcess = 1;
        LogError( ( "snprintf return unexpected value %d", written ) );
    }
    else if( written == remainSize )
    {
        skipProcess = 1;
        LogError( ( "buffer has no space for session attributes" ) );
    }
    else
    {
        pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].pAttributeName = pCurBuffer;
        pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].attributeNameLength = strlen( pCurBuffer );

        pCurBuffer += written;
        remainSize -= written;
    }

    if( !skipProcess )
    {
        written = snprintf( pCurBuffer, remainSize, "BUNDLE 0 1" );
        if( written < 0 )
        {
            skipProcess = 1;
            LogError( ( "snprintf return unexpected value %d", written ) );
        }
        else if( written == remainSize )
        {
            skipProcess = 1;
            LogError( ( "buffer has no space for session attributes" ) );
        }
        else
        {
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].pAttributeValue = pCurBuffer;
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].attributeValueLength = strlen( pCurBuffer );

            pCurBuffer += written;
            remainSize -= written;

            pSdpDescription->sessionAttributesCount++;
        }
    }

    /* ice-options. */
    if( !skipProcess )
    {
        written = snprintf( pCurBuffer, remainSize, "ice-options" );
        if( written < 0 )
        {
            skipProcess = 1;
            LogError( ( "snprintf return unexpected value %d", written ) );
        }
        else if( written == remainSize )
        {
            skipProcess = 1;
            LogError( ( "buffer has no space for session attributes" ) );
        }
        else
        {
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].pAttributeName = pCurBuffer;
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].attributeNameLength = strlen( pCurBuffer );

            pCurBuffer += written;
            remainSize -= written;
        }
    }

    if( !skipProcess )
    {
        written = snprintf( pCurBuffer, remainSize, "trickle" );
        if( written < 0 )
        {
            skipProcess = 1;
            LogError( ( "snprintf return unexpected value %d", written ) );
        }
        else if( written == remainSize )
        {
            skipProcess = 1;
            LogError( ( "buffer has no space for session attributes" ) );
        }
        else
        {
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].pAttributeValue = pCurBuffer;
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].attributeValueLength = strlen( pCurBuffer );

            pCurBuffer += written;
            remainSize -= written;

            pSdpDescription->sessionAttributesCount++;
        }
    }

    /* msid-semantic */
    if( !skipProcess )
    {
        written = snprintf( pCurBuffer, remainSize, "msid-semantic" );
        if( written < 0 )
        {
            skipProcess = 1;
            LogError( ( "snprintf return unexpected value %d", written ) );
        }
        else if( written == remainSize )
        {
            skipProcess = 1;
            LogError( ( "buffer has no space for session attributes" ) );
        }
        else
        {
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].pAttributeName = pCurBuffer;
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].attributeNameLength = strlen( pCurBuffer );

            pCurBuffer += written;
            remainSize -= written;
        }
    }

    if( !skipProcess )
    {
        written = snprintf( pCurBuffer, remainSize, " WMS myKvsVideoStream" );
        if( written < 0 )
        {
            skipProcess = 1;
            LogError( ( "snprintf return unexpected value %d", written ) );
        }
        else if( written == remainSize )
        {
            skipProcess = 1;
            LogError( ( "buffer has no space for session attributes" ) );
        }
        else
        {
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].pAttributeValue = pCurBuffer;
            pSdpDescription->attributes[ pSdpDescription->sessionAttributesCount ].attributeValueLength = strlen( pCurBuffer );

            pCurBuffer += written;
            remainSize -= written;

            pSdpDescription->sessionAttributesCount++;
        }
    }

    if( !skipProcess )
    {
        *ppBuffer = pCurBuffer;
        *pBufferLength = remainSize;
    }

    return skipProcess;
}

static uint8_t populateMediaAttributes( char **ppBuffer, size_t *pBufferLength, SdpControllerSdpDescription_t *pSdpDescription )
{
    uint8_t skipProcess = 0;

    /* Audio */
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].pMediaName = "audio 9 UDP/TLS/RTP/SAVPF 111";
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaNameLength = strlen( "audio 9 UDP/TLS/RTP/SAVPF 111" );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].pMediaTitle = NULL;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaTitleLength = 0;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.pNetworkType = SDP_CONTROLLER_ORIGIN_DEFAULT_NET_TYPE;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.networkTypeLength = strlen( SDP_CONTROLLER_ORIGIN_DEFAULT_NET_TYPE );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.pAddressType = SDP_CONTROLLER_ORIGIN_IPV4_TYPE;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.addressTypeLength = strlen( SDP_CONTROLLER_ORIGIN_IPV4_TYPE );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.pConnectionAddress = SDP_CONTROLLER_ORIGIN_DEFAULT_IP_ADDRESS;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.connectionAddressLength = strlen( SDP_CONTROLLER_ORIGIN_DEFAULT_IP_ADDRESS );

    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute0Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute0Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute0Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute0Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute1Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute1Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute1Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute1Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute2Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute2Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute2Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute2Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute3Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute3Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute3Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute3Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute4Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute4Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute4Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute4Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute5Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute5Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute5Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute5Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute6Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute6Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute6Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute6Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute7Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute7Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute7Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute7Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute8Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute8Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute8Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute8Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute9Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute9Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute9Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute9Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute10Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute10Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute10Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute10Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute11Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute11Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute11Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute11Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute12Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute12Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute12Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute12Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute13Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute13Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute14Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute14Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute15Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute15Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute15Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute15Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute16Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute16Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute16Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute16Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute17Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute17Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute17Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute17Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = audioAttribute18Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( audioAttribute18Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = audioAttribute18Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( audioAttribute18Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;

    pSdpDescription->mediaCount++;

    /* Video */
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].pMediaName = "video 9 UDP/TLS/RTP/SAVPF 106";
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaNameLength = strlen( "video 9 UDP/TLS/RTP/SAVPF 106" );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].pMediaTitle = NULL;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaTitleLength = 0;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.pNetworkType = SDP_CONTROLLER_ORIGIN_DEFAULT_NET_TYPE;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.networkTypeLength = strlen( SDP_CONTROLLER_ORIGIN_DEFAULT_NET_TYPE );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.pAddressType = SDP_CONTROLLER_ORIGIN_IPV4_TYPE;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.addressTypeLength = strlen( SDP_CONTROLLER_ORIGIN_IPV4_TYPE );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.pConnectionAddress = SDP_CONTROLLER_ORIGIN_DEFAULT_IP_ADDRESS;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].connectionInformation.connectionAddressLength = strlen( SDP_CONTROLLER_ORIGIN_DEFAULT_IP_ADDRESS );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute0Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute0Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute0Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute0Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute1Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute1Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute1Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute1Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute2Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute2Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute2Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute2Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute3Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute3Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute3Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute3Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute4Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute4Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute4Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute4Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute5Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute5Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute5Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute5Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute6Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute6Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute6Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute6Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute7Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute7Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute7Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute7Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute8Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute8Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute8Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute8Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute9Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute9Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute9Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute9Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute10Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute10Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute10Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute10Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute11Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute11Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute11Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute11Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute12Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute12Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute12Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute12Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute13Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute13Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute14Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute14Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute15Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute15Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute16Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute16Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute16Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute16Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute17Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute17Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute17Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute17Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute18Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute18Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute18Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute18Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute19Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute19Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute19Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute19Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeName = videoAttribute20Name;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeNameLength = strlen( videoAttribute20Name );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].pAttributeValue = videoAttribute20Value;
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].attributes[ pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount ].attributeValueLength = strlen( videoAttribute20Value );
    pSdpDescription->mediaDescriptions[ pSdpDescription->mediaCount ].mediaAttributesCount++;

    pSdpDescription->mediaCount++;

    return skipProcess;
}

static uint8_t prepareSdpAnswer( DemoSessionInformation_t *pSessionInDescriptionOffer, DemoSessionInformation_t *pSessionInDescriptionAnswer )
{
    uint8_t skipProcess = 0;
    size_t remainLength = DEMO_SDP_BUFFER_MAX_LENGTH, offset=0;
    char *pSdpBuffer = &pSessionInDescriptionAnswer->sdpBuffer[0];
    
    if( pSessionInDescriptionOffer == NULL || pSessionInDescriptionAnswer == NULL )
    {
        skipProcess = 1;
    }

    if( !skipProcess )
    {
        memset( pSessionInDescriptionAnswer, 0, sizeof( DemoSessionInformation_t ) );

        /* Session version. */
        pSessionInDescriptionAnswer->sdpDescription.version = 0U;

        /* Session origin. */
        populateSessionOrigin( &pSdpBuffer, &remainLength, &pSessionInDescriptionAnswer->sdpDescription.origin );

        /* Session name. */
        pSessionInDescriptionAnswer->sdpDescription.pSessionName = SDP_CONTROLLER_ORIGIN_DEFAULT_SESSION_NAME;
        pSessionInDescriptionAnswer->sdpDescription.sessionNameLength = strlen( SDP_CONTROLLER_ORIGIN_DEFAULT_SESSION_NAME );

        /* Session timing description. */
        pSessionInDescriptionAnswer->sdpDescription.timingDescription.startTime = 0U;
        pSessionInDescriptionAnswer->sdpDescription.timingDescription.stopTime = 0U;

        /* Session attributes. */
        skipProcess = populateSessionAttributes( &pSdpBuffer, &remainLength, &pSessionInDescriptionAnswer->sdpDescription );
    }

    if( !skipProcess )
    {
        /* Media attributes. */
        skipProcess = populateMediaAttributes( &pSdpBuffer, &remainLength, &pSessionInDescriptionAnswer->sdpDescription );
    }

    return skipProcess;
}

static uint8_t serializeSdpMessage( DemoSessionInformation_t *pSessionInDescriptionAnswer )
{
    uint8_t skipProcess = 0;
    SdpControllerResult_t retSdpController;

    demoContext.sdpConstructedBufferLength = DEMO_SDP_BUFFER_MAX_LENGTH;
    retSdpController = SdpController_SerializeSdpMessage( SDP_CONTROLLER_MESSAGE_TYPE_ANSWER, &pSessionInDescriptionAnswer->sdpDescription, demoContext.sdpConstructedBuffer, &demoContext.sdpConstructedBufferLength );
    if( retSdpController != SDP_CONTROLLER_RESULT_OK )
    {
        LogError( ( "SdpController_SerializeSdpMessage fail, returns %d", retSdpController ) );
        skipProcess = 1;
    }

    if( !skipProcess )
    {
        pSessionInDescriptionAnswer->sdpBufferLength = DEMO_SDP_BUFFER_MAX_LENGTH;
        retSdpController = SdpController_SerializeSdpNewline( demoContext.sdpConstructedBuffer, demoContext.sdpConstructedBufferLength, pSessionInDescriptionAnswer->sdpBuffer, &pSessionInDescriptionAnswer->sdpBufferLength );
        if( retSdpController != SDP_CONTROLLER_RESULT_OK )
        {
            LogError( ( "SdpController_SerializeSdpNewline fail, returns %d", retSdpController ) );
            skipProcess = 1;
        }
        else
        {
            LogDebug( ( "Serialized SDP answer (%lu):\n%.*s", pSessionInDescriptionAnswer->sdpBufferLength,
                                                              ( int ) pSessionInDescriptionAnswer->sdpBufferLength,
                                                              pSessionInDescriptionAnswer->sdpBuffer ) );
        }
    }

    return skipProcess;
}

static uint8_t addressSdpOffer( const char *pEventSdpOffer, size_t eventSdpOfferlength )
{
    uint8_t skipProcess = 0;

    skipProcess = storeAndParseSdpOffer( pEventSdpOffer, eventSdpOfferlength, &demoContext.sessionInformationSdpOffer );

    return skipProcess;
}

static void respondWithSdpAnswer( const char *pRemoteClientId, size_t remoteClientIdLength )
{
    uint8_t skipProcess = 0;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerEventMessage_t eventMessage = {
        .event = SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE,
        .onCompleteCallback = NULL,
        .pOnCompleteCallbackContext = NULL,
    };

    /* Prepare SDP answer and send it back to remote peer. */
    skipProcess = prepareSdpAnswer( &demoContext.sessionInformationSdpOffer, &demoContext.sessionInformationSdpAnswer );

    if( !skipProcess )
    {
        skipProcess = serializeSdpMessage( &demoContext.sessionInformationSdpAnswer );
    }

    if( !skipProcess )
    {
        eventMessage.eventContent.correlationIdLength = 0U;
        memset( eventMessage.eventContent.correlationId, 0, SIGNALING_CONTROLLER_CORRELATION_ID_MAX_LENGTH );
        eventMessage.eventContent.messageType = SIGNALING_TYPE_MESSAGE_SDP_ANSWER;
        eventMessage.eventContent.pDecodeMessage = demoContext.sessionInformationSdpAnswer.sdpBuffer;
        eventMessage.eventContent.decodeMessageLength = demoContext.sessionInformationSdpAnswer.sdpBufferLength;
        memcpy( eventMessage.eventContent.remoteClientId, pRemoteClientId, remoteClientIdLength );
        eventMessage.eventContent.remoteClientIdLength = remoteClientIdLength;
        
        signalingControllerReturn = SignalingController_SendMessage( &signalingControllerContext, &eventMessage );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            skipProcess = 1;
            LogError( ( "Send signaling message fail, result: %d", signalingControllerReturn ) );
        }
    }
}

int32_t handleSignalingMessage( SignalingControllerReceiveEvent_t *pEvent, void *pUserContext )
{
    uint8_t skipProcess = 0;

    ( void ) pUserContext;

    LogInfo( ( "Received Message from websocket server!" ) );
    LogDebug( ( "Message Type: %x", pEvent->messageType ) );
    LogDebug( ( "Sender ID: %.*s", ( int ) pEvent->remoteClientIdLength, pEvent->pRemoteClientId ) );
    LogDebug( ( "Correlation ID: %.*s", ( int ) pEvent->correlationIdLength, pEvent->pCorrelationId ) );
    LogDebug( ( "Message Length: %ld, Message:", pEvent->decodeMessageLength ) );
    LogDebug( ( "%.*s", ( int ) pEvent->decodeMessageLength, pEvent->pDecodeMessage ) );

    switch( pEvent->messageType )
    {
        case SIGNALING_TYPE_MESSAGE_SDP_OFFER:
            skipProcess = addressSdpOffer( pEvent->pDecodeMessage, pEvent->decodeMessageLength );

            if( !skipProcess )
            {
                respondWithSdpAnswer( pEvent->pRemoteClientId, pEvent->remoteClientIdLength );
            }
            break;
        case SIGNALING_TYPE_MESSAGE_SDP_ANSWER:
            break;
        case SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE:
            break;
        case SIGNALING_TYPE_MESSAGE_GO_AWAY:
            break;
        case SIGNALING_TYPE_MESSAGE_RECONNECT_ICE_SERVER:
            break;
        case SIGNALING_TYPE_MESSAGE_STATUS_RESPONSE:
            break;
        default:
            break;
    }

    return 0;
}

int main()
{
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerCredential_t signalingControllerCred;

    memset( &demoContext, 0, sizeof( DemoContext_t ) );

    memset( &signalingControllerCred, 0, sizeof(SignalingControllerCredential_t) );
    signalingControllerCred.pRegion = AWS_REGION;
    signalingControllerCred.regionLength = strlen( AWS_REGION );
    signalingControllerCred.pChannelName = AWS_KVS_CHANNEL_NAME;
    signalingControllerCred.channelNameLength = strlen( AWS_KVS_CHANNEL_NAME );
    signalingControllerCred.pUserAgentName = AWS_KVS_AGENT_NAME;
    signalingControllerCred.userAgentNameLength = strlen(AWS_KVS_AGENT_NAME);
    signalingControllerCred.pAccessKeyId = AWS_ACCESS_KEY_ID;
    signalingControllerCred.accessKeyIdLength = strlen(AWS_ACCESS_KEY_ID);
    signalingControllerCred.pSecretAccessKey = AWS_SECRET_ACCESS_KEY;
    signalingControllerCred.secretAccessKeyLength = strlen(AWS_SECRET_ACCESS_KEY);
    signalingControllerCred.pCaCertPath = AWS_CA_CERT_PATH;

    signalingControllerReturn = SignalingController_Init( &signalingControllerContext, &signalingControllerCred, handleSignalingMessage, NULL );

    signal( SIGINT, terminateHandler );

    if( signalingControllerReturn == SIGNALING_CONTROLLER_RESULT_OK )
    {
        signalingControllerReturn = SignalingController_ConnectServers( &signalingControllerContext );
    }

    if( signalingControllerReturn == SIGNALING_CONTROLLER_RESULT_OK )
    {
        /* This should never return unless exception happens. */
        signalingControllerReturn = SignalingController_ProcessLoop( &signalingControllerContext );
    }

    return 0;
}
