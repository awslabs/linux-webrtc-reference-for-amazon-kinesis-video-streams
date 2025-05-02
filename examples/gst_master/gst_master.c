#include "demo_gst_master_data_types.h"

#include <../common/common.c>

static int32_t InitializeGstMediaSource(DemoContext_t* pDemoContext)
{
    LogDebug(("InitializeGstMediaSource"));
    int32_t ret = 0;

    if (pDemoContext == NULL)
    {
        LogError(("Invalid input, pDemoContext: %p", pDemoContext));
        ret = -1;
    }

    if (ret == 0)
    {
        ret = GstMediaSource_Init(&pDemoContext->mediaSourceContext,
            OnMediaSinkHook,
            pDemoContext);
    }

    return ret;
}

static int32_t StartPeerConnectionSession( DemoContext_t * pDemoContext,
                                           DemoPeerConnectionSession_t * pDemoSession,
                                           const char * pRemoteClientId,
                                           size_t remoteClientIdLength )
{
    int32_t ret = 0;
    PeerConnectionResult_t peerConnectionResult;
    PeerConnectionSessionConfiguration_t pcConfig;
    Transceiver_t * pTransceiver = NULL;

    if( remoteClientIdLength > REMOTE_ID_MAX_LENGTH )
    {
        LogWarn( ( "The remote client ID length(%lu) is too long to store.", remoteClientIdLength ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        memset( &pcConfig, 0, sizeof( PeerConnectionSessionConfiguration_t ) );
        pcConfig.iceServersCount = ICE_CONTROLLER_MAX_ICE_SERVER_COUNT;
        #if defined( AWS_CA_CERT_PATH )
            pcConfig.pRootCaPath = AWS_CA_CERT_PATH;
            pcConfig.rootCaPathLength = strlen( AWS_CA_CERT_PATH );
        #endif /* #if defined( AWS_CA_CERT_PATH ) */

        #if defined( AWS_CA_CERT_PEM )
            pcConfig.rootCaPem = AWS_CA_CERT_PEM;
            pcConfig.rootCaPemLength = sizeof( AWS_CA_CERT_PEM );
        #endif /* #if defined( AWS_CA_CERT_PEM ) */

        pcConfig.canTrickleIce = 1U;
        pcConfig.natTraversalConfigBitmap = ICE_CANDIDATE_NAT_TRAVERSAL_CONFIG_ALLOW_ALL;

        ret = GetIceServerList( pDemoContext,
                                pcConfig.iceServers,
                                &pcConfig.iceServersCount );
    }

    if( ret == 0 )
    {
        peerConnectionResult = PeerConnection_AddIceServerConfig( &pDemoSession->peerConnectionSession,
                                                                  &pcConfig );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_AddIceServerConfig fail, result: %d", peerConnectionResult ) );
            ret = -1;
        }
    }

    if( ret == 0 )
    {
        peerConnectionResult = PeerConnection_SetOnLocalCandidateReady( &pDemoSession->peerConnectionSession,
                                                                        HandleLocalCandidateReady,
                                                                        pDemoSession );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogWarn( ( "PeerConnection_SetOnLocalCandidateReady fail, result: %d", peerConnectionResult ) );
            ret = -1;
        }
    }

    /* Add video transceiver */
    if( ret == 0 )
    {
        pTransceiver = &pDemoSession->transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO ];
        ret = GstMediaSource_InitVideoTransceiver( &pDemoContext->mediaSourceContext,
                                                   pTransceiver );
        if( ret != 0 )
        {
            LogError( ( "Fail to get video transceiver." ) );
        }
        else
        {
            peerConnectionResult = PeerConnection_AddTransceiver( &pDemoSession->peerConnectionSession,
                                                                  pTransceiver );
            if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
            {
                LogError( ( "Fail to add video transceiver, result = %d.", peerConnectionResult ) );
                ret = -1;
            }
        }
    }

    /* Add audio transceiver */
    if( ret == 0 )
    {
        pTransceiver = &pDemoSession->transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO ];
        ret = GstMediaSource_InitAudioTransceiver( &pDemoContext->mediaSourceContext,
                                                   pTransceiver );
        if( ret != 0 )
        {
            LogError( ( "Fail to get audio transceiver." ) );
        }
        else
        {
            peerConnectionResult = PeerConnection_AddTransceiver( &pDemoSession->peerConnectionSession,
                                                                  pTransceiver );
            if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
            {
                LogError( ( "Fail to add audio transceiver, result = %d.", peerConnectionResult ) );
                ret = -1;
            }
        }
    }

    if( ret == 0 )
    {
        pDemoSession->remoteClientIdLength = remoteClientIdLength;
        memcpy( pDemoSession->remoteClientId, pRemoteClientId, remoteClientIdLength );
        peerConnectionResult = PeerConnection_Start( &pDemoSession->peerConnectionSession );
        if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
        {
            LogError( ( "Fail to start peer connection, result = %d.", peerConnectionResult ) );
            ret = -1;
        }
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

    memset( &demoContext, 0, sizeof( DemoContext_t ) );
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
        /* Set the signal handler to release resource correctly. */
        signal( SIGINT, terminateHandler );

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
