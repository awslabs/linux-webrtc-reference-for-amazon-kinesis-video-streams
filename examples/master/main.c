#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "demo_config.h"
#include "signaling_controller.h"

SignalingControllerContext_t signalingControllerContext;

int32_t handleSignalingMessage( SignalingControllerReceiveEvent_t *pEvent, void *pUserContext )
{
    ( void ) pUserContext;
    printf( "Received Message from websocket server!\n"
            "Message Type: %x\n"
            "Sender ID: %.*s\n"
            "Correlation ID: %.*s\n"
            "Message: Length %ld\n"
            "%.*s\n",
            pEvent->messageType,
            ( int ) pEvent->senderClientIdLength, pEvent->pSenderClientId,
            ( int ) pEvent->correlationIdLength, pEvent->pCorrelationId,
            pEvent->decodeMessageLength,
            ( int ) pEvent->decodeMessageLength, pEvent->pDecodeMessage );
    return 0;
}

int main()
{
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerCredential_t signalingControllerCred;

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
