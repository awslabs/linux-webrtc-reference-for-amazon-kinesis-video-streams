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

#define IS_USERNAME_FOUND_BIT ( 1 << 0 )
#define IS_PASSWORD_FOUND_BIT ( 1 << 1 )
#define SET_REMOTE_INFO_USERNAME_FOUND( isFoundBit ) ( isFoundBit |= IS_USERNAME_FOUND_BIT )
#define SET_REMOTE_INFO_PASSWORD_FOUND( isFoundBit ) ( isFoundBit |= IS_PASSWORD_FOUND_BIT )
#define IS_REMOTE_INFO_ALL_FOUND( isFoundBit ) ( isFoundBit & IS_USERNAME_FOUND_BIT && isFoundBit & IS_PASSWORD_FOUND_BIT )

DemoContext_t demoContext;

extern uint8_t prepareSdpAnswer( DemoSessionInformation_t *pSessionInDescriptionOffer, DemoSessionInformation_t *pSessionInDescriptionAnswer );
extern uint8_t serializeSdpMessage( DemoSessionInformation_t *pSessionInDescriptionAnswer, DemoContext_t *pDemoContext );
extern uint8_t addressSdpOffer( const char *pEventSdpOffer, size_t eventSdpOfferlength, DemoContext_t *pDemoContext );

static void terminateHandler( int sig )
{
    SignalingController_Deinit( &demoContext.signalingControllerContext );
    exit( 0 );
}

static uint8_t respondWithSdpAnswer( const char *pRemoteClientId, size_t remoteClientIdLength, DemoContext_t *pDemoContext )
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

    return skipProcess;
}

static int initializeIceController( DemoContext_t *pDemoContext )
{
    int ret = 0;
    IceControllerResult_t iceControllerReturn;

    iceControllerReturn = IceController_Init( &pDemoContext->iceControllerContext, &pDemoContext->signalingControllerContext );
    if( iceControllerReturn != ICE_CONTROLLER_RESULT_OK )
    {
        LogError( ( "Fail to initialize ice controller." ) );
        ret = -1;
    }

    return ret;
}

static uint8_t searchUserNamePassWord( SdpControllerAttributes_t *pAttributes, size_t attributesCount,
                                       const char **ppRemoteUserName, size_t *pRemoteUserNameLength, const char **ppRemotePassword, size_t *pRemotePasswordLength )
{
    uint8_t isFound = 0;
    size_t i;

    for( i=0 ; i<attributesCount ; i++ )
    {
        if( pAttributes[i].attributeNameLength == strlen( "ice-ufrag" ) &&
            strncmp( pAttributes[i].pAttributeName, "ice-ufrag", pAttributes[i].attributeNameLength ) == 0 )
        {
            /* Found user name. */
            SET_REMOTE_INFO_USERNAME_FOUND( isFound );
            *ppRemoteUserName = pAttributes[i].pAttributeValue;
            *pRemoteUserNameLength = pAttributes[i].attributeValueLength;
        }
        else if( pAttributes[i].attributeNameLength == strlen( "ice-pwd" ) &&
                 strncmp( pAttributes[i].pAttributeName, "ice-pwd", pAttributes[i].attributeNameLength ) == 0 )
        {
            /* Found password. */
            SET_REMOTE_INFO_PASSWORD_FOUND( isFound );
            *ppRemotePassword = pAttributes[i].pAttributeValue;
            *pRemotePasswordLength = pAttributes[i].attributeValueLength;
        }
        else
        {
            continue;
        }

        if( IS_REMOTE_INFO_ALL_FOUND( isFound ) )
        {
            break;
        }
    }

    return isFound;
}

static uint8_t getRemoteInfo( DemoSessionInformation_t *pSessionInformation, const char **ppRemoteUserName, size_t *pRemoteUserNameLength, const char **ppRemotePassword, size_t *pRemotePasswordLength )
{
    uint8_t skipProcess = 0;
    SdpControllerSdpDescription_t *pSessionDescription = &pSessionInformation->sdpDescription;
    size_t i;
    uint8_t isFound;

    /* Assuming that the username & password in a single session description is same. */
    /* Search session attributes first. */
    isFound = searchUserNamePassWord( pSessionDescription->attributes, pSessionDescription->sessionAttributesCount,
                                      ppRemoteUserName, pRemoteUserNameLength,
                                      ppRemotePassword, pRemotePasswordLength );

    if( !IS_REMOTE_INFO_ALL_FOUND( isFound ) )
    {
        /* Search media attributes if not found in session attributes. */
        for( i=0 ; i<pSessionDescription->mediaCount ; i++ )
        {
            isFound = 0;
            isFound = searchUserNamePassWord( pSessionDescription->mediaDescriptions[i].attributes, pSessionDescription->mediaDescriptions[i].mediaAttributesCount,
                                              ppRemoteUserName, pRemoteUserNameLength,
                                              ppRemotePassword, pRemotePasswordLength );
            if( IS_REMOTE_INFO_ALL_FOUND( isFound ) )
            {
                break;
            }
        }
    }

    if( !IS_REMOTE_INFO_ALL_FOUND( isFound ) )
    {
        /* Can't find user name & pass word, drop this remote description. */
        LogWarn( ( "No remote username & password found in session description, drop this message" ) );
        skipProcess = 1;
    }

    return skipProcess;
}

static uint8_t setRemoteDescription( IceControllerContext_t *pIceControllerCtx, DemoSessionInformation_t *pSessionInformation, const char *pRemoteClientId, size_t remoteClientIdLength )
{
    uint8_t skipProcess = 0;
    const char *pRemoteUserName;
    size_t remoteUserNameLength;
    const char *pRemotePassword;
    size_t remotePasswordLength;
    IceControllerResult_t iceControllerResult;

    skipProcess = getRemoteInfo( pSessionInformation, &pRemoteUserName, &remoteUserNameLength, &pRemotePassword, &remotePasswordLength );

    if( skipProcess == 0 )
    {
        iceControllerResult = IceController_SetRemoteDescription( pIceControllerCtx, pRemoteClientId, remoteClientIdLength, pRemoteUserName, remoteUserNameLength, pRemotePassword, remotePasswordLength );
        if( iceControllerResult != 0 )
        {
            LogError( ( "Fail to set remote description, result: %d", iceControllerResult ) );
            skipProcess = 1;
        }
    }

    return skipProcess;
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
                skipProcess = respondWithSdpAnswer( pEvent->pRemoteClientId, pEvent->remoteClientIdLength, &demoContext );
            }

            if( !skipProcess )
            {
                skipProcess = setRemoteDescription( &demoContext.iceControllerContext, &demoContext.sessionInformationSdpOffer, pEvent->pRemoteClientId, pEvent->remoteClientIdLength );
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
