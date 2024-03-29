#include <stdio.h>
#include <string.h>
#include "demo_config.h"
#include "signaling_controller.h"

SignalingControllerContext_t signalingControllerContext;

int main()
{
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerCredential_t signalingControllerCred;

    memset( &signalingControllerCred, 0, sizeof(SignalingControllerCredential_t) );
    signalingControllerCred.pAccessKeyId = AWS_ACCESS_KEY_ID;
    signalingControllerCred.accessKeyIdLength = strlen(AWS_ACCESS_KEY_ID);
    signalingControllerCred.pSecretAccessKey = AWS_SECRET_ACCESS_KEY;
    signalingControllerCred.secretAccessKeyLength = strlen(AWS_SECRET_ACCESS_KEY);
    signalingControllerReturn = SignalingController_Init( &signalingControllerContext, &signalingControllerCred );

    if( signalingControllerReturn == SIGNALING_CONTROLLER_RESULT_OK )
    {
        signalingControllerReturn = SignalingController_ConnectServers( &signalingControllerContext );
    }

    return 0;
}
