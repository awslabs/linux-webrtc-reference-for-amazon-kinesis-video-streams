#include "demo_config.h"
#include "demo_gst_master_data_types.h"
#include "app_common.h"

static int32_t OnMediaSinkHook( void * pCustom,
                                   WebrtcFrame_t * pFrame )
{
    int32_t ret = 0;
    AppContext_t * pAppContext = ( AppContext_t * ) pCustom;
    PeerConnectionResult_t peerConnectionResult;
    Transceiver_t * pTransceiver = NULL;
    PeerConnectionFrame_t peerConnectionFrame;
    int i;

    if( ( pAppContext == NULL ) || ( pFrame == NULL ) )
    {
        LogError( ( "Invalid input, pCustom: %p, pFrame: %p", pCustom, pFrame ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        peerConnectionFrame.version = PEER_CONNECTION_FRAME_CURRENT_VERSION;
        peerConnectionFrame.presentationUs = pFrame->timestampUs;
        peerConnectionFrame.pData = pFrame->pData;
        peerConnectionFrame.dataLength = pFrame->size;

        for( i = 0; i < AWS_MAX_VIEWER_NUM; i++ )
        {
            if( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO )
            {
                pTransceiver = &pAppContext->appSessions[ i ].transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO ];
            }
            else if( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_AUDIO )
            {
                pTransceiver = &pAppContext->appSessions[ i ].transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO ];
            }
            else
            {
                /* Unknown kind, skip that. */
                LogWarn( ( "Unknown track kind: %d", pFrame->trackKind ) );
                break;
            }

            if( pAppContext->appSessions[ i ].peerConnectionSession.state == PEER_CONNECTION_SESSION_STATE_CONNECTION_READY )
            {
                peerConnectionResult = PeerConnection_WriteFrame( &pAppContext->appSessions[ i ].peerConnectionSession,
                                                                  pTransceiver,
                                                                  &peerConnectionFrame );

                if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
                {
                    LogError( ( "Fail to write %s frame, result: %d", ( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO ) ? "video" : "audio",
                                peerConnectionResult ) );
                    ret = -3;
                }
            }
        }
    }

    return ret;
}

static int32_t InitializeGstMediaSource(AppContext_t* pAppContext)
{
    LogDebug(("InitializeGstMediaSource"));
    int32_t ret = 0;

    if (pAppContext == NULL)
    {
        LogError(("Invalid input, pAppContext: %p", pAppContext));
        ret = -1;
    }

    if (ret == 0)
    {
        ret = GstMediaSource_Init(&pAppContext->mediaSourceContext,
            OnMediaSinkHook,
            pAppContext);
    }

    return ret;
}

int main()
{
    int ret = 0;
    SignalingControllerResult_t signalingControllerReturn;
    SignalingControllerConnectInfo_t connectInfo;
    SSLCredentials_t sslCreds;
    int i;

    srand( time( NULL ) );

    srtp_init();

    #if ENABLE_SCTP_DATA_CHANNEL
        Sctp_Init();
    #endif /* ENABLE_SCTP_DATA_CHANNEL */

    memset( &demoContext, 0, sizeof( AppContext_t ) );
    memset( &sslCreds, 0, sizeof( SSLCredentials_t ) );
    memset( &connectInfo, 0, sizeof( SignalingControllerConnectInfo_t ) );

    sslCreds.pCaCertPath = AWS_CA_CERT_PATH;
    #if defined( AWS_IOT_THING_ROLE_ALIAS )
        sslCreds.pDeviceCertPath = AWS_IOT_THING_CERT_PATH;
        sslCreds.pDeviceKeyPath = AWS_IOT_THING_PRIVATE_KEY_PATH;
    #else
        sslCreds.pDeviceCertPath = NULL;
        sslCreds.pDeviceKeyPath = NULL;
    #endif
    #if ( JOIN_STORAGE_SESSION != 0 )
        connectInfo.enableStorageSession = 1U;
    #endif
    connectInfo.awsConfig.pRegion = AWS_REGION;
    connectInfo.awsConfig.regionLen = strlen( AWS_REGION );
    connectInfo.awsConfig.pService = "kinesisvideo";
    connectInfo.awsConfig.serviceLen = strlen( "kinesisvideo" );

    connectInfo.channelName.pChannelName = AWS_KVS_CHANNEL_NAME;
    connectInfo.channelName.channelNameLength = strlen( AWS_KVS_CHANNEL_NAME );

    connectInfo.pUserAgentName = AWS_KVS_AGENT_NAME;
    connectInfo.userAgentNameLength = strlen( AWS_KVS_AGENT_NAME );

    connectInfo.messageReceivedCallback = OnSignalingMessageReceived;
    connectInfo.pMessageReceivedCallbackData = NULL;

    #if defined( AWS_ACCESS_KEY_ID )
        connectInfo.awsCreds.pAccessKeyId = AWS_ACCESS_KEY_ID;
        connectInfo.awsCreds.accessKeyIdLen = strlen( AWS_ACCESS_KEY_ID );
        connectInfo.awsCreds.pSecretAccessKey = AWS_SECRET_ACCESS_KEY;
        connectInfo.awsCreds.secretAccessKeyLen = strlen( AWS_SECRET_ACCESS_KEY );
        #if defined( AWS_SESSION_TOKEN )
            connectInfo.awsCreds.pSessionToken = AWS_SESSION_TOKEN;
            connectInfo.awsCreds.sessionTokenLength = strlen( AWS_SESSION_TOKEN );
        #endif /* #if defined( AWS_SESSION_TOKEN ) */
    #endif /* #if defined( AWS_ACCESS_KEY_ID ) */

    #if defined( AWS_IOT_THING_ROLE_ALIAS )
        connectInfo.awsIotCreds.pIotCredentialsEndpoint = AWS_CREDENTIALS_ENDPOINT;
        connectInfo.awsIotCreds.iotCredentialsEndpointLength = strlen( AWS_CREDENTIALS_ENDPOINT );
        connectInfo.awsIotCreds.pThingName = AWS_IOT_THING_NAME;
        connectInfo.awsIotCreds.thingNameLength = strlen( AWS_IOT_THING_NAME );
        connectInfo.awsIotCreds.pRoleAlias = AWS_IOT_THING_ROLE_ALIAS;
        connectInfo.awsIotCreds.roleAliasLength = strlen( AWS_IOT_THING_ROLE_ALIAS );
    #endif /* #if defined( AWS_IOT_THING_ROLE_ALIAS ) */

    signalingControllerReturn = SignalingController_Init( &demoContext.signalingControllerContext,
                                                          &sslCreds );

    if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
    {
        LogError( ( "Fail to initialize signaling controller." ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        /* Initialize metrics. */
        Metric_Init();
    }

    if( ret == 0 )
    {
        ret = InitializeGstMediaSource( &demoContext );
    }

    if( ret == 0 )
    {
        for( i = 0; i < AWS_MAX_VIEWER_NUM; i++ )
        {
            ret = InitializePeerConnectionSession( &demoContext,
                                                   &demoContext.peerConnectionSessions[i] );
            if( ret != 0 )
            {
                LogError( ( "Fail to initialize peer connection sessions." ) );
                break;
            }
        }
    }

    if( ret == 0 )
    {
        /* This should never return unless exception happens. */
        signalingControllerReturn = SignalingController_StartListening( &demoContext.signalingControllerContext,
                                                                        &connectInfo );
        if( signalingControllerReturn != SIGNALING_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to keep processing signaling controller." ) );
            ret = -1;
        }
    }

    #if ENABLE_SCTP_DATA_CHANNEL
        /* TODO_SCTP: Move to a common shutdown function? */
        Sctp_DeInit();
    #endif /* ENABLE_SCTP_DATA_CHANNEL */

    return 0;
}
