#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "demo_config.h"
#include "logging.h"
#include "demo_data_types.h"
#include "signaling_controller.h"
#include "sdp_controller.h"

DemoContext_t demoContext;

extern uint8_t prepareSdpAnswer( DemoSessionInformation_t *pSessionInDescriptionOffer, DemoSessionInformation_t *pSessionInDescriptionAnswer );
extern uint8_t serializeSdpMessage( DemoSessionInformation_t *pSessionInDescriptionAnswer, DemoContext_t *pDemoContext );
extern uint8_t addressSdpOffer( const char *pEventSdpOffer, size_t eventSdpOfferlength, DemoContext_t *pDemoContext );

static void terminateHandler( int sig )
{
    SignalingController_Deinit( &demoContext.signalingControllerContext );
    exit( 0 );
}

static void respondWithSdpAnswer( const char *pRemoteClientId, size_t remoteClientIdLength, DemoContext_t *pDemoContext )
{
    uint8_t skipProcess = 0;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerEventMessage_t eventMessage = {
        .event = SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE,
        .onCompleteCallback = NULL,
        .pOnCompleteCallbackContext = NULL,
    };

    /* Prepare SDP answer and send it back to remote peer. */
    skipProcess = prepareSdpAnswer( &pDemoContext->sessionInformationSdpOffer, &pDemoContext->sessionInformationSdpAnswer );

    if( !skipProcess )
    {
        skipProcess = serializeSdpMessage( &pDemoContext->sessionInformationSdpAnswer, pDemoContext );
    }

    if( !skipProcess )
    {
        eventMessage.eventContent.correlationIdLength = 0U;
        memset( eventMessage.eventContent.correlationId, 0, SIGNALING_CONTROLLER_CORRELATION_ID_MAX_LENGTH );
        eventMessage.eventContent.messageType = SIGNALING_TYPE_MESSAGE_SDP_ANSWER;
        eventMessage.eventContent.pDecodeMessage = pDemoContext->sessionInformationSdpAnswer.sdpBuffer;
        eventMessage.eventContent.decodeMessageLength = pDemoContext->sessionInformationSdpAnswer.sdpBufferLength;
        memcpy( eventMessage.eventContent.remoteClientId, pRemoteClientId, remoteClientIdLength );
        eventMessage.eventContent.remoteClientIdLength = remoteClientIdLength;
        
        signalingControllerReturn = SignalingController_SendMessage( &demoContext.signalingControllerContext, &eventMessage );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            skipProcess = 1;
            LogError( ( "Send signaling message fail, result: %d", signalingControllerReturn ) );
        }
    }
}

static int initializeIceController( DemoContext_t *pDemoContext )
{
    int ret = 0;
    IceControllerResult_t iceControllerReturn;

    iceControllerReturn = IceController_Init( &pDemoContext->iceControllerContext );
    if( iceControllerReturn != ICE_CONTROLLER_RESULT_OK )
    {
        LogError( ( "Fail to initialize ice controller." ) );
        ret = -1;
    }

    return ret;
}

int32_t handleSignalingMessage( SignalingControllerReceiveEvent_t *pEvent, void *pUserContext )
{
    uint8_t skipProcess = 0;
    IceControllerResult_t iceControllerResult;
    IceControllerCandidate_t candidate;

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
            skipProcess = addressSdpOffer( pEvent->pDecodeMessage, pEvent->decodeMessageLength, &demoContext );

            if( !skipProcess )
            {
                respondWithSdpAnswer( pEvent->pRemoteClientId, pEvent->remoteClientIdLength, &demoContext );
            }
            break;
        case SIGNALING_TYPE_MESSAGE_SDP_ANSWER:
            break;
        case SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE:
            iceControllerResult = IceController_DeserializeIceCandidate( pEvent->pDecodeMessage, pEvent->decodeMessageLength, &candidate );

            if( iceControllerResult != ICE_CONTROLLER_RESULT_OK )
            {
                LogWarn( ( "IceController_DeserializeIceCandidate fail, dropping ICE candidate." ) );
            }
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

typedef struct demoIceCandidate
{
    char *pCandidate;
    size_t candidateLength;
    int32_t sdpMid;
    int32_t sdpMLineIndex;
    char *pUsernameFragment;
    size_t usernameFragmentLength;
} demoIceCandidate_t;

#define ICE_CANDIDATE_JSON_TEMPLATE "{\"candidate\":\"%.*s\",\"sdpMid\":\"%d\",\"sdpMLineIndex\":%d,\"usernameFragment\":\"%.*s\"}"
#define ICE_CANDIDATE_JSON_MAX_LENGTH ( 1024 )

int32_t sendIceCandidateCompleteCallback( SignalingControllerEventStatus_t status, void *pUserContext )
{
    free( pUserContext );

    return 0;
}

int32_t sendIceCandidate( const char *pRemoteClientId, size_t remoteClientIdLength, demoIceCandidate_t *pIceCandidate )
{
    int32_t ret = 0;
    int written;
    char *pBuffer;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerEventMessage_t eventMessage = {
        .event = SIGNALING_CONTROLLER_EVENT_SEND_WSS_MESSAGE,
        .onCompleteCallback = sendIceCandidateCompleteCallback,
        .pOnCompleteCallbackContext = NULL,
    };

    if( pIceCandidate == NULL )
    {
        ret = -1;
    }

    if( ret == 0 )
    {
        /* Format this into candidate string. */
        pBuffer = ( char * ) malloc( ICE_CANDIDATE_JSON_MAX_LENGTH );
        memset( pBuffer, 0, sizeof( pBuffer ) );

        written = snprintf( pBuffer, ICE_CANDIDATE_JSON_MAX_LENGTH, ICE_CANDIDATE_JSON_TEMPLATE,
                            ( int ) pIceCandidate->candidateLength, pIceCandidate->pCandidate,
                            pIceCandidate->sdpMid,
                            pIceCandidate->sdpMLineIndex,
                            ( int ) pIceCandidate->usernameFragmentLength, pIceCandidate->pUsernameFragment );

        if( written < 0 )
        {
            LogError( ( "snprintf returns fail, errno: %d", errno ) );
            ret = -2;
        }
    }

    if( ret == 0 )
    {
        eventMessage.eventContent.correlationIdLength = 0U;
        eventMessage.eventContent.messageType = SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE;
        eventMessage.eventContent.pDecodeMessage = pBuffer;
        eventMessage.eventContent.decodeMessageLength = written;
        memcpy( eventMessage.eventContent.remoteClientId, pRemoteClientId, remoteClientIdLength );
        eventMessage.eventContent.remoteClientIdLength = remoteClientIdLength;

        /* We dynamically allocate buffer for signaling controller to keep using it.
         * callback it as context to free memory. */
        eventMessage.pOnCompleteCallbackContext = pBuffer;
        
        signalingControllerReturn = SignalingController_SendMessage( &demoContext.signalingControllerContext, &eventMessage );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Send signaling message fail, result: %d", signalingControllerReturn ) );
            ret = -3;
        }
    }

    return ret;
}

int main()
{
    int ret = 0;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerCredential_t signalingControllerCred;

    srand( time( NULL ) );

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

    signalingControllerReturn = SignalingController_Init( &demoContext.signalingControllerContext, &signalingControllerCred, handleSignalingMessage, NULL );
    if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
    {
        LogError( ( "Fail to initialize signaling controller." ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        /* Set the signal handler to release resource correctly. */
        signal( SIGINT, terminateHandler );

        /* Initialize Ice controller. */
        ret = initializeIceController( &demoContext );
    }

    if( ret == 0 )
    {
        signalingControllerReturn = SignalingController_ConnectServers( &demoContext.signalingControllerContext );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to connect with signaling controller." ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        /* This should never return unless exception happens. */
        signalingControllerReturn = SignalingController_ProcessLoop( &demoContext.signalingControllerContext );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to keep processing signaling controller." ) );
            ret = -1;
        }
    }

    return 0;
}
