#include <stdio.h>
#include <string.h>
#include "demo_config.h"
#include "logging.h"
#include "demo_data_types.h"
#include "signaling_controller.h"
#include "sdp_controller.h"

DemoContext_t demoContext;
SignalingControllerContext_t signalingControllerContext;

static uint8_t storeAndParseSdpOffer( const char *pEventSdpOffer, size_t eventSdpOfferlength, DemoSessionInformation_t *pSessionInformation )
{
    uint8_t skipProcess = 0;
    SdpControllerResult_t retSdpController;
    const char *pSdpContent;
    size_t sdpContentLength;
    char *pSdpOffer = pSessionInformation->sdpOfferBuffer;
    size_t *pSdpOfferLength = &pSessionInformation->sdpOfferBufferLength;

    /* Store the SDP offer then parse it in poiters structure. */
    retSdpController = SdpController_GetSdpOfferContent( pEventSdpOffer, eventSdpOfferlength, &pSdpContent, &sdpContentLength );
    if( retSdpController != SDP_CONTROLLER_RESULT_OK )
    {
        LogError( ( "Unable to find SDP offer content, result: %d", retSdpController ) );
        skipProcess = 1;
    }

    if( !skipProcess )
    {
        if( sdpContentLength > DEMO_SDP_OFFER_BUFFER_MAX_LENGTH )
        {
            LogError( ( "No enough memory to store SDP offer" ) );
            skipProcess = 1;
        }
        else
        {
            /* Keep SDP concent in global buffer after replacing newline, then we can keep accessing the parsed result in pointers structure. */
            demoContext.sessionInformation.sdpOfferBufferLength = DEMO_SDP_OFFER_BUFFER_MAX_LENGTH;
            retSdpController = SdpController_ConvertSdpContentNewline( pSdpContent, sdpContentLength,
                                                                       &pSdpOffer, pSdpOfferLength );
            if( retSdpController != SDP_CONTROLLER_RESULT_OK )
            {
                skipProcess = 1;
                LogError( ( "Unable to convert SDP offer, result: %d", retSdpController ) );
            }
        }
    }

    if( !skipProcess )
    {
        retSdpController = SdpController_DeserializeSdpOffer( pSessionInformation->sdpOfferBuffer,
                                                              pSessionInformation->sdpOfferBufferLength,
                                                              &pSessionInformation->sdpOffer );
        if( retSdpController != SDP_CONTROLLER_RESULT_OK )
        {
            skipProcess = 1;
            LogError( ( "Unable to deserialize SDP offer, result: %d", retSdpController ) );
        }
    }

    return skipProcess;
}

static void handleSdpOffer( const char *pEventSdpOffer, size_t eventSdpOfferlength )
{
    uint8_t skipProcess = 0;

    skipProcess = storeAndParseSdpOffer( pEventSdpOffer, eventSdpOfferlength, &demoContext.sessionInformation );

    if( !skipProcess )
    {
        /* Prepare SDP answer and send it back to remote peer. */
        // createSdpAnswer( &demoContext.sessionInformation );
    }
}

int32_t handleSignalingMessage( SignalingControllerReceiveEvent_t *pEvent, void *pUserContext )
{
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
            handleSdpOffer( pEvent->pDecodeMessage, pEvent->decodeMessageLength );
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
