#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "logging.h"
#include "peer_connection.h"
#include "peer_connection_srtp.h"
#include "peer_connection_sdp.h"
#include "rtp_api.h"
#include "rtcp_api.h"
#include "peer_connection_rolling_buffer.h"
#include "metric.h"

#include "peer_connection_h264_helper.h"
#include "peer_connection_opus_helper.h"
#include "peer_connection_g711_helper.h"

#if ENABLE_SCTP_DATA_CHANNEL
#include "peer_connection_sctp.h"
#endif

#define PEER_CONNECTION_SESSION_TASK_NAME "PcSessionTsk"
#define PEER_CONNECTION_SESSION_RX_TASK_NAME "PcRxTsk" // For Ice controller to monitor socket Rx path
#define PEER_CONNECTION_MESSAGE_QUEUE_NAME "/PcSessionMq"

#define PEER_CONNECTION_MAX_QUEUE_MSG_NUM ( 10 )

#define PEER_CONNECTION_MAX_DTLS_DECRYPTED_DATA_LENGTH ( 2048 )

PeerConnectionContext_t peerConnectionContext = { 0 };

extern void * IceControllerSocketListener_Task( void * pParameter );
static void * PeerConnection_SessionTask( void * pParameter );
static void SessionProcessEndlessLoop( PeerConnectionSession_t * pSession );
static void HandleRequest( PeerConnectionSession_t * pSession,
                           MessageQueueHandler_t * pRequestQueue );
static PeerConnectionResult_t HandleAddRemoteCandidateRequest( PeerConnectionSession_t * pSession,
                                                               PeerConnectionSessionRequestMessage_t * pRequestMessage );
static PeerConnectionResult_t HandleConnectivityCheckRequest( PeerConnectionSession_t * pSession,
                                                              PeerConnectionSessionRequestMessage_t * pRequestMessage );
static int32_t StartDtlsHandshake( PeerConnectionSession_t * pSession );
static int32_t ExecuteDtlsHandshake( PeerConnectionSession_t * pSession );
static int32_t OnDtlsHandshakeComplete( PeerConnectionSession_t * pSession );

static void * PeerConnection_SessionTask( void * pParameter )
{
    PeerConnectionSession_t * pSession = ( PeerConnectionSession_t * ) pParameter;

    for( ; pSession->state < PEER_CONNECTION_SESSION_STATE_START; )
    {
        usleep( 50 * 1000 );
    }

    LogInfo( ( "Start peer connection session task." ) );

    SessionProcessEndlessLoop( pSession );

    for( ;; )
    {
        LogError( ( "PeerConnectionTask returns unexpectly." ) );
        //vTaskDelay( pdMS_TO_TICKS( 2000 ) );
        usleep( 2 * 1000 * 1000 );
    }

    return 0;
}

static void SessionProcessEndlessLoop( PeerConnectionSession_t * pSession )
{
    uint8_t skipProcess = 0;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        skipProcess = 1;
    }

    for( ; !skipProcess; )
    {
        HandleRequest( pSession,
                       &pSession->requestQueue );

        /* If a P2P connection is found and DTLS handshaking is in progress,
         * invoke the handshake here to retry and prevent packet loss in transit. */
        if( pSession->state == PEER_CONNECTION_SESSION_STATE_P2P_CONNECTION_FOUND )
        {
            ( void ) ExecuteDtlsHandshake( pSession );
        }
    }
}

static void HandleRequest( PeerConnectionSession_t * pSession,
                           MessageQueueHandler_t * pRequestQueue )
{
    MessageQueueResult_t retMessageQueue;
    PeerConnectionSessionRequestMessage_t requestMsg;
    size_t requestMsgLength;

    /* Handle event. */
    requestMsgLength = sizeof( PeerConnectionSessionRequestMessage_t );
    retMessageQueue = MessageQueue_Recv( pRequestQueue,
                                         &requestMsg,
                                         &requestMsgLength );
    if( retMessageQueue == MESSAGE_QUEUE_RESULT_OK )
    {
        /* Received message, process it. */
        LogDebug( ( "Receive request type: %d", requestMsg.requestType ) );
        switch( requestMsg.requestType )
        {
            case PEER_CONNECTION_SESSION_REQUEST_TYPE_ADD_REMOTE_CANDIDATE:
                ( void ) HandleAddRemoteCandidateRequest( pSession,
                                                          &requestMsg );
                break;
            case PEER_CONNECTION_SESSION_REQUEST_TYPE_CONNECTIVITY_CHECK:
                ( void ) HandleConnectivityCheckRequest( pSession,
                                                         &requestMsg );
                break;
            default:
                /* Unknown request, drop it. */
                LogDebug( ( "Dropping unknown request %d", requestMsg.requestType ) );
                break;
        }
    }
}

static PeerConnectionResult_t HandleAddRemoteCandidateRequest( PeerConnectionSession_t * pSession,
                                                               PeerConnectionSessionRequestMessage_t * pRequestMessage )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    IceControllerResult_t iceControllerResult;
    IceControllerCandidate_t * pRemoteCandidate = ( IceControllerCandidate_t * )&pRequestMessage->peerConnectionSessionRequestContent;
    IceRemoteCandidateInfo_t remoteCandidateInfo;

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        remoteCandidateInfo.candidateType = pRemoteCandidate->candidateType;
        remoteCandidateInfo.pEndpoint = &( pRemoteCandidate->iceEndpoint );
        remoteCandidateInfo.priority = pRemoteCandidate->priority;
        remoteCandidateInfo.remoteProtocol = pRemoteCandidate->protocol;

        iceControllerResult = IceController_AddRemoteCandidate( &pSession->iceControllerContext, &remoteCandidateInfo );
        if( iceControllerResult != ICE_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to add remote candidate." ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_ADD_REMOTE_CANDIDATE;
        }
    }

    return ret;
}

static PeerConnectionResult_t HandleConnectivityCheckRequest( PeerConnectionSession_t * pSession,
                                                              PeerConnectionSessionRequestMessage_t * pRequestMessage )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    IceControllerResult_t iceControllerResult;

    ( void ) pRequestMessage;

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        iceControllerResult = IceController_SendConnectivityCheck( &pSession->iceControllerContext );
        if( iceControllerResult != ICE_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to add remote candidate." ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_SEND_CONNECTIVITY_CHECK;
        }
    }

    return ret;
}

static PeerConnectionResult_t SendRemoteCandidateRequest( PeerConnectionSession_t * pSession,
                                                          IceControllerCandidate_t * pRemoteCandidate )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    MessageQueueResult_t retMessageQueue;
    IceControllerCandidate_t * pMessageContent;
    PeerConnectionSessionRequestMessage_t requestMessage = {
        .requestType = PEER_CONNECTION_SESSION_REQUEST_TYPE_ADD_REMOTE_CANDIDATE,
    };

    if( ( pSession == NULL ) || ( pRemoteCandidate == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pRemoteCandidate: %p", pSession, pRemoteCandidate ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pMessageContent = &requestMessage.peerConnectionSessionRequestContent.remoteCandidate;
        memcpy( pMessageContent,
                pRemoteCandidate,
                sizeof( IceControllerCandidate_t ) );

        retMessageQueue = MessageQueue_Send( &pSession->requestQueue,
                                             &requestMessage,
                                             sizeof( PeerConnectionSessionRequestMessage_t ) );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to send message queue, error: %d", retMessageQueue ) );
            ret = PEER_CONNECTION_RESULT_FAIL_MQ_SEND;
        }
    }

    return ret;
}

static int32_t OnIceEventConnectivityCheck( PeerConnectionSession_t * pSession )
{
    int32_t ret = 0;
    MessageQueueResult_t retMessageQueue;
    PeerConnectionSessionRequestMessage_t requestMessage = {
        .requestType = PEER_CONNECTION_SESSION_REQUEST_TYPE_CONNECTIVITY_CHECK,
    };

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = -10;
    }

    if( ret == 0 )
    {
        retMessageQueue = MessageQueue_Send( &pSession->requestQueue,
                                             &requestMessage,
                                             sizeof( PeerConnectionSessionRequestMessage_t ) );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            ret = -11;
        }
    }

    return ret;
}

static int32_t HandleDtlsTermination( PeerConnectionSession_t * pSession )
{
    int32_t ret = 0;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = -10;
    }

    if( ret == 0 )
    {
        DTLS_Disconnect( &pSession->dtlsSession.xNetworkContext );
        LogInfo( ( "DTLS_Disconnect called successfully" ) );

        /* Close the socket context to avoid any input packets triggering unexpected RTP/RTCP handling. */
        PeerConnection_CloseSession( pSession );
    }

    return ret;
}

static int32_t OnDtlsSendHook( void * pCustomContext,
                               const uint8_t * pBuffer,
                               size_t bufferLength )
{
    int32_t ret = 0;
    PeerConnectionSession_t * pSession = ( PeerConnectionSession_t * ) pCustomContext;
    IceControllerResult_t resultIceController = ICE_CONTROLLER_RESULT_OK;

    resultIceController = IceController_SendToRemotePeer( &pSession->iceControllerContext,
                                                          pBuffer,
                                                          bufferLength );
    if( resultIceController != ICE_CONTROLLER_RESULT_OK )
    {
        LogWarn( ( "Fail to send DTLS packet, ret: %d", resultIceController ) );
        ret = -1;
    }
    else
    {
        ret = bufferLength;
    }

    return ret;
}

static int32_t HandleIceEventCallback( void * pCustomContext,
                                       IceControllerCallbackEvent_t event,
                                       IceControllerCallbackContent_t * pEventMsg )
{
    int32_t ret = 0;
    PeerConnectionSession_t * pSession = ( PeerConnectionSession_t * ) pCustomContext;
    PeerConnectionIceLocalCandidate_t * pLocalCandidateReadyMsg = NULL;

    if( ( pCustomContext == NULL ) )
    {
        LogError( ( "Invalid input, pCustomContext: %p, pEventMsg: %p",
                    pCustomContext, pEventMsg ) );
        ret = -1;
    }
    else if( ( event == ICE_CONTROLLER_CB_EVENT_NONE ) || ( event >= ICE_CONTROLLER_CB_EVENT_MAX ) )
    {
        LogError( ( "Unknown event: %d",
                    event ) );
        ret = -2;
    }
    else
    {
        switch( event )
        {
            case ICE_CONTROLLER_CB_EVENT_LOCAL_CANDIDATE_READY:
                if( pEventMsg != NULL )
                {
                    if( pSession->onIceCandidateReadyCallbackFunc != NULL )
                    {
                        pLocalCandidateReadyMsg = ( PeerConnectionIceLocalCandidate_t * ) &pEventMsg->iceControllerCallbackContent.localCandidateReadyMsg;
                        pSession->onIceCandidateReadyCallbackFunc( pSession->pOnLocalCandidateReadyCallbackCustomContext, pLocalCandidateReadyMsg );
                    }
                    else
                    {
                        LogError( ( "No proper callback function to handle local candidate ready message." ) );
                    }
                }
                else
                {
                    LogError( ( "Event message pointer must be valid in event: %d.", event ) );
                }
                break;
            case ICE_CONTROLLER_CB_EVENT_CONNECTIVITY_CHECK_TIMEOUT:
                ret = OnIceEventConnectivityCheck( pSession );
                break;
            case ICE_CONTROLLER_CB_EVENT_PEER_TO_PEER_CONNECTION_FOUND:
                /* Assign transport send/recv callback function/context for TURN headers. */
                memset( &pSession->dtlsSession.xDtlsTransportParams, 0, sizeof( DtlsTransportParams_t ) );
                pSession->state = PEER_CONNECTION_SESSION_STATE_P2P_CONNECTION_FOUND;
                pSession->dtlsSession.xDtlsTransportParams.onDtlsSendHook = OnDtlsSendHook;
                pSession->dtlsSession.xDtlsTransportParams.pOnDtlsSendCustomContext = ( void * ) pSession;

                /* Start DTLS handshaking. */
                Metric_StartEvent( METRIC_EVENT_PC_DTLS_HANDSHAKING );
                ret = StartDtlsHandshake( pSession );

                /* This must set after StartDtlsHandshake, or the other thread might execute handshake earlier than expectation. */
                pSession->state = PEER_CONNECTION_SESSION_STATE_P2P_CONNECTION_FOUND;
                break;
            default:
                LogError( ( "Unknown event: %d", event ) );
                break;
        }
    }

    return ret;
}

static int32_t StartDtlsHandshake( PeerConnectionSession_t * pSession )
{
    int32_t ret = 0;
    DtlsTransportStatus_t xNetworkStatus = DTLS_SUCCESS;
    DtlsSession_t * pDtlsSession = NULL;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = -20;
    }

    if( ret == 0 )
    {
        /* Set the pParams member of the network context with desired transport. */
        pDtlsSession = &pSession->dtlsSession;
        pDtlsSession->xNetworkContext.pParams = &pDtlsSession->xDtlsTransportParams;

        // /* Set the network credentials. */
        /* Disable SNI server name indication*/
        // https://mbed-tls.readthedocs.io/en/latest/kb/how-to/use-sni/
        pDtlsSession->xNetworkCredentials.disableSni = 1;
    }

    if( ret == 0 )
    {
        if( NULL == pSession->pCtx->dtlsContext.localCert.raw.p )
        {
            LogError( ( "Fail to get answer cert: NULL == pSession->pCtx->dtlsContext.localCert.raw.p" ) );
            ret = -23;
        }
        else
        {
            /* Assign local cert to the DTLS session. */
            LogDebug( ( "setting pDtlsSession->xNetworkCredentials.pClientCert" ) );
            pDtlsSession->xNetworkCredentials.pClientCert = &pSession->pCtx->dtlsContext.localCert;

            // /* Assign local key to the DTLS session. */
            LogDebug( ( "setting pDtlsSession->xNetworkCredentials.pPrivateKey" ) );
            pDtlsSession->xNetworkCredentials.pPrivateKey = &pSession->pCtx->dtlsContext.localKey;

            /* Attempt to create a DTLS connection. */
            xNetworkStatus = DTLS_Init( &pDtlsSession->xNetworkContext,
                                        &pDtlsSession->xNetworkCredentials,
                                        0U );

            if( xNetworkStatus != DTLS_SUCCESS )
            {
                LogError( ( "Fail to initialize the DTLS session with return %d ", xNetworkStatus ) );
                ret = -24;
            }
        }
    }

    if( ret == 0 )
    {
        /* Start the DTLS handshaking. */
        ret = ExecuteDtlsHandshake( pSession );
    }

    return ret;
}

static int32_t ExecuteDtlsHandshake( PeerConnectionSession_t * pSession )
{
    int32_t ret = 0;
    DtlsTransportStatus_t xNetworkStatus = DTLS_SUCCESS;
    DtlsSession_t * pDtlsSession = NULL;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = -30;
    }

    if( ret == 0 )
    {
        pDtlsSession = &pSession->dtlsSession;

        /* Trigger the DTLS handshaking to send client hello if necessary. */
        xNetworkStatus = DTLS_ExecuteHandshake( &pDtlsSession->xNetworkContext );

        if( xNetworkStatus == DTLS_HANDSHAKE_COMPLETE )
        {
            ret = OnDtlsHandshakeComplete( pSession );
        }
        else if( ( xNetworkStatus != DTLS_SUCCESS ) &&
                 ( xNetworkStatus != DTLS_HANDSHAKE_ALREADY_COMPLETE ) )
        {
            LogError( ( "Error happens when executing DTLS handshake, return %d", xNetworkStatus ) );
            ret = -31;
        }
        else
        {
            /* This condition means DTLS handshaking is not complete yet. Wait for Rx packet. */
        }
    }

    return ret;
}

static int32_t OnDtlsHandshakeComplete( PeerConnectionSession_t * pSession )
{
    int32_t ret = 0;
    DtlsTransportStatus_t xNetworkStatus = DTLS_SUCCESS;
    PeerConnectionResult_t retPc;
    uint32_t i;

    LogDebug( ( "Complete DTLS handshaking." ) );
    Metric_EndEvent( METRIC_EVENT_PC_DTLS_HANDSHAKING );

    /* Verify remote fingerprint (if remote cert fingerprint is the expected one) */
    xNetworkStatus = DTLS_VerifyRemoteCertificateFingerprint( &pSession->dtlsSession.xNetworkContext.pParams->dtlsSslContext,
                                                              pSession->remoteCertFingerprint,
                                                              pSession->remoteCertFingerprintLength );

    if( xNetworkStatus != DTLS_SUCCESS )
    {
        LogError( ( "Fail to DTLS_VerifyRemoteCertificateFingerprint with return %d ", xNetworkStatus ) );
        ret = -0x1001;
    }

    if( ret == 0 )
    {
        /* Retrieve key material into DTLS session. */
        xNetworkStatus = DTLS_PopulateKeyingMaterial( &pSession->dtlsSession.xNetworkContext.pParams->dtlsSslContext,
                                                      &pSession->dtlsSession.xNetworkCredentials.dtlsKeyingMaterial );

        if( xNetworkStatus != DTLS_SUCCESS )
        {
            LogError( ( "Fail to DTLS_PopulateKeyingMaterial with return %d ", xNetworkStatus ) );
            ret = -0x1002;
        }
        else
        {
            LogDebug( ( "DTLS_PopulateKeyingMaterial with key_length: %i ", pSession->dtlsSession.xNetworkCredentials.dtlsKeyingMaterial.key_length ) );
        }
    }

    if( ret == 0 )
    {
        /* Initialize SRTP sessions. */
        retPc = PeerConnectionSrtp_Init( pSession );
        if( retPc != PEER_CONNECTION_RESULT_OK )
        {
            LogError( ( "Fail to create SRTP sessions, ret: %d", retPc ) );
            ret = -0x1003;
        }
    }

    #if ENABLE_SCTP_DATA_CHANNEL
    if( ret == 0 )
    {
        /* Initialize SCTP sessions. */
        if( pSession->ucEnableDataChannelRemote == 1 )
        {
            retPc = PeerConnectionSCTP_AllocateSCTP( pSession );
            if( retPc != PEER_CONNECTION_RESULT_OK )
            {
                LogError( ( "Fail to create SCTP sessions, ret: %d", retPc ) );
                ret = -0x1004;
            }
        }
    }
    #endif /* ENABLE_SCTP_DATA_CHANNEL */

    if( ret == 0 )
    {
        pSession->state = PEER_CONNECTION_SESSION_STATE_CONNECTION_READY;
        for( i = 0; i < pSession->transceiverCount; i++ )
        {
            if( pSession->pTransceivers[i]->onPcEventCallbackFunc )
            {
                pSession->pTransceivers[i]->onPcEventCallbackFunc( pSession->pTransceivers[i]->pOnPcEventCustomContext,
                                                                   TRANSCEIVER_CB_EVENT_REMOTE_PEER_READY,
                                                                   NULL );
            }
        }
    }

    return ret;
}

static int32_t ProcessDtlsPacket( PeerConnectionSession_t * pSession,
                                  uint8_t * pDtlsEncryptData,
                                  size_t dtlsEncryptDataLength )
{
    int32_t ret = 0;
    DtlsTransportStatus_t xNetworkStatus = DTLS_SUCCESS;
    uint8_t dtlsDecryptBuffer[ PEER_CONNECTION_MAX_DTLS_DECRYPTED_DATA_LENGTH ];
    size_t dtlsDecryptBufferLength = PEER_CONNECTION_MAX_DTLS_DECRYPTED_DATA_LENGTH;

    xNetworkStatus = DTLS_ProcessPacket( &pSession->dtlsSession.xNetworkContext,
                                         pDtlsEncryptData,
                                         dtlsEncryptDataLength,
                                         dtlsDecryptBuffer,
                                         &dtlsDecryptBufferLength );

    if( xNetworkStatus == DTLS_HANDSHAKE_COMPLETE )
    {
        ret = OnDtlsHandshakeComplete( pSession );
    }
    else if( xNetworkStatus == DTLS_SUCCESS )
    {
        #if ENABLE_SCTP_DATA_CHANNEL
        {
            if( pSession->state == PEER_CONNECTION_SESSION_STATE_CONNECTION_READY )
            {
                PeerConnectionSCTP_ProcessSCTPData( pSession, dtlsDecryptBuffer, dtlsDecryptBufferLength );
            }
        }
        #endif /* ENABLE_SCTP_DATA_CHANNEL */
    }
    else if( xNetworkStatus != DTLS_SUCCESS )
    {
        LogInfo( ( "Error happens when process the DTLS packet, return %d", xNetworkStatus ) );
        ret = HandleDtlsTermination( pSession );
    }
    else
    {
        /* Empty else marker. */
    }

    return ret;
}

static int32_t HandleNonStunPackets( void * pCustomContext,
                                     uint8_t * pBuffer,
                                     size_t bufferLength )
{
    int32_t ret = 0;
    PeerConnectionSession_t * pSession = ( PeerConnectionSession_t * ) pCustomContext;
    PeerConnectionResult_t resultPeerConnection;

    if( ( pCustomContext == NULL ) || ( pBuffer == NULL ) )
    {
        LogWarn( ( "Invalid input, pCustomContext: %p, pBuffer: %p",
                   pCustomContext, pBuffer ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        if( bufferLength < 2 )
        {
            LogWarn( ( "Invalid buffer length: %lu", bufferLength ) );
            ret = -2;
        }
        else if( ( pBuffer[0] > 127 ) && ( pBuffer[0] < 192 ) )
        {
            if( ( pBuffer[1] >= 192 ) && ( pBuffer[1] <= 223 ) )
            {
                /* RTCP packet */
                resultPeerConnection = PeerConnectionSrtp_HandleSrtcpPacket( pSession, pBuffer, bufferLength );
                if( resultPeerConnection != PEER_CONNECTION_RESULT_OK )
                {
                    LogWarn( ( "Failed to handle SRTCP packets, result: %d", resultPeerConnection ) );
                    ret = -2;
                }
            }
            else
            {
                /* RTP packet */
                resultPeerConnection = PeerConnectionSrtp_HandleSrtpPacket( pSession, pBuffer, bufferLength );
                if( resultPeerConnection != PEER_CONNECTION_RESULT_OK )
                {
                    LogWarn( ( "Failed to handle SRTP packets, result: %d", resultPeerConnection ) );
                    ret = -2;
                }
            }
        }
        else if( ( pBuffer[0] > 19 ) && ( pBuffer[0] < 64 ) )
        {
            /* Trigger the DTLS handshaking to send client hello if necessary
             * and process incoming DTLS data by forwarding to respective
             * libraries to process. */
            ret = ProcessDtlsPacket( pSession, pBuffer, bufferLength );
        }
        else
        {
            LogWarn( ( "drop unknown DTLS packet, length=%lu, first byte=%u", bufferLength, pBuffer[0] ) );
        }
    }

    return ret;
}

/* Generate a printable string that does not
 * need to be escaped when encoding in JSON
 */
static void generateJSONValidString( char * pDst,
                                     size_t length )
{
    size_t i = 0;
    uint8_t skipProcess = 0;
    const char jsonCharSet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
    const size_t jsonCharSetLength = strlen( jsonCharSet );

    if( pDst == NULL )
    {
        skipProcess = 1;
    }

    if( skipProcess == 0 )
    {
        for( i = 0; i < length; i++ )
        {
            pDst[i] = jsonCharSet[ rand() % jsonCharSetLength ];
        }
    }
}

static PeerConnectionResult_t InitializeIceController( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    IceControllerResult_t iceControllerResult;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p",
                    pSession ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        iceControllerResult = IceController_Init( &pSession->iceControllerContext,
                                                  HandleIceEventCallback,
                                                  pSession,
                                                  HandleNonStunPackets,
                                                  pSession );
        if( iceControllerResult != ICE_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to initialize Ice Controller." ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_INIT;
        }
    }

    return ret;
}

static PeerConnectionResult_t DestroyIceController( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    IceControllerResult_t iceControllerResult;

    if( pSession == NULL )
    {
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        iceControllerResult = IceController_Destroy( &pSession->iceControllerContext );
        if( iceControllerResult != ICE_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to destroy Ice Controller." ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_DESTROY;
        }
    }

    return ret;
}

static PeerConnectionResult_t AllocateTransceiver( PeerConnectionSession_t * pSession,
                                                   Transceiver_t * pTransceiver )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        if( pSession->transceiverCount < PEER_CONNECTION_TRANSCEIVER_MAX_COUNT )
        {
            pTransceiver->ssrc = ( uint32_t ) rand();
            pTransceiver->rtxSsrc = ( uint32_t ) rand();
            pSession->pTransceivers[ pSession->transceiverCount ] = pTransceiver;
            pSession->transceiverCount++;
        }
        else
        {
            LogWarn( ( "No space to add transceiver" ) );
            ret = PEER_CONNECTION_RESULT_NO_FREE_TRANSCEIVER;
        }
    }

    return ret;
}

static PeerConnectionResult_t InitializeDtlsContext( PeerConnectionDtlsContext_t * pDtlsContext )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    DtlsTransportStatus_t xNetworkStatus = DTLS_SUCCESS;

    if( pDtlsContext == NULL )
    {
        LogError( ( "Invalid input, pDtlsContext: %p", pDtlsContext ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    /* Generate local cert in DER format. */
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        xNetworkStatus = DTLS_CreateCertificateAndKey( GENERATED_CERTIFICATE_BITS,
                                                       0,
                                                       &pDtlsContext->localCert,
                                                       &pDtlsContext->localKey );
        if( xNetworkStatus != DTLS_SUCCESS )
        {
            LogError( ( "Fail to DTLS_CreateCertificateAndKey, return %d", xNetworkStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_CREATE_CERT_AND_KEY;
        }
    }

    // Generate cert fingerprint
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        xNetworkStatus = DTLS_CreateCertificateFingerprint( &pDtlsContext->localCert,
                                                            pDtlsContext->localCertFingerprint,
                                                            CERTIFICATE_FINGERPRINT_LENGTH );
        if( xNetworkStatus != DTLS_SUCCESS )
        {
            LogError( ( "Fail to dtlsCertificateFingerprint answer cert, return %d", xNetworkStatus ) );
            ret = PEER_CONNECTION_RESULT_FAIL_CREATE_CERT_FINGERPRINT;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pDtlsContext->isInitialized = 1;
    }

    return ret;
}

static PeerConnectionResult_t GetDefaultCodec( uint32_t codecBitMap,
                                               uint32_t * pOutputCodec )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( TRANSCEIVER_IS_CODEC_ENABLED( codecBitMap, TRANSCEIVER_RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_BIT ) )
    {
        *pOutputCodec = TRANSCEIVER_RTC_CODEC_DEFAULT_PAYLOAD_H264;
    }
    else if( TRANSCEIVER_IS_CODEC_ENABLED( codecBitMap, TRANSCEIVER_RTC_CODEC_OPUS_BIT ) )
    {
        *pOutputCodec = TRANSCEIVER_RTC_CODEC_DEFAULT_PAYLOAD_OPUS;
    }
    else if( TRANSCEIVER_IS_CODEC_ENABLED( codecBitMap, TRANSCEIVER_RTC_CODEC_VP8_BIT ) )
    {
        *pOutputCodec = TRANSCEIVER_RTC_CODEC_DEFAULT_PAYLOAD_VP8;
    }
    else if( TRANSCEIVER_IS_CODEC_ENABLED( codecBitMap, TRANSCEIVER_RTC_CODEC_MULAW_BIT ) )
    {
        *pOutputCodec = TRANSCEIVER_RTC_CODEC_DEFAULT_PAYLOAD_MULAW;
    }
    else if( TRANSCEIVER_IS_CODEC_ENABLED( codecBitMap, TRANSCEIVER_RTC_CODEC_ALAW_BIT ) )
    {
        *pOutputCodec = TRANSCEIVER_RTC_CODEC_DEFAULT_PAYLOAD_ALAW;
    }
    else if( TRANSCEIVER_IS_CODEC_ENABLED( codecBitMap, TRANSCEIVER_RTC_CODEC_H265_BIT ) )
    {
        *pOutputCodec = TRANSCEIVER_RTC_CODEC_DEFAULT_PAYLOAD_H265;
    }
    else
    {
        ret = PEER_CONNECTION_RESULT_UNKNOWN_CODEC;
        LogError( ( "No default codec found." ) );
    }

    return ret;
}

static PeerConnectionResult_t SetDefaultPayloadTypes( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    int i;
    const Transceiver_t * pTransceiver = NULL;

    LogInfo( ( "Setting default payload types for session, transceiverCount: %u",
               pSession->transceiverCount ) );
    for( i = 0; i < pSession->transceiverCount && ret == PEER_CONNECTION_RESULT_OK; i++ )
    {
        pTransceiver = pSession->pTransceivers[i];

        if( pTransceiver == NULL )
        {
            LogError( ( "This is not expected, keep this condition here to avoid NULL accessing, transceiverCount: %u, current index: %d",
                        pSession->transceiverCount,
                        i ) );
            ret = PEER_CONNECTION_RESULT_UNKNOWN_TRANSCEIVER;
            break;
        }
        else if( pTransceiver->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO )
        {
            pSession->rtpConfig.isVideoCodecPayloadSet = 1;
            pSession->rtpConfig.videoCodecRtxPayload = 0;
            pSession->rtpConfig.videoRtxSequenceNumber = 0;
            pSession->rtpConfig.videoSequenceNumber = 0;
            ret = GetDefaultCodec( pTransceiver->codecBitMap, &pSession->rtpConfig.videoCodecPayload );
        }
        else
        {
            pSession->rtpConfig.isAudioCodecPayloadSet = 1;
            pSession->rtpConfig.audioCodecRtxPayload = 0;
            pSession->rtpConfig.audioRtxSequenceNumber = 0;
            pSession->rtpConfig.audioSequenceNumber = 0;
            ret = GetDefaultCodec( pTransceiver->codecBitMap, &pSession->rtpConfig.audioCodecPayload );
        }
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_Init( PeerConnectionSession_t * pSession,
                                            PeerConnectionSessionConfiguration_t * pSessionConfig )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    MessageQueueResult_t retMessageQueue;
    char tempName[ 20 ];
    DtlsSession_t * pDtlsSession = NULL;
    static uint8_t initSeq = 0;

    /* Avoid unused variable warning. */
    ( void ) pSessionConfig;

    if( ( pSession == NULL ) || ( pSessionConfig == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pSessionConfig: %p", pSession, pSessionConfig ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ( ret == PEER_CONNECTION_RESULT_OK ) &&
        ( peerConnectionContext.isInited == 0U ) )
    {
        peerConnectionContext.isInited = 1U;

        generateJSONValidString( peerConnectionContext.localUserName,
                                 PEER_CONNECTION_USER_NAME_LENGTH );
        peerConnectionContext.localUserName[ PEER_CONNECTION_USER_NAME_LENGTH ] = '\0';
        generateJSONValidString( peerConnectionContext.localPassword,
                                 PEER_CONNECTION_PASSWORD_LENGTH );
        peerConnectionContext.localPassword[ PEER_CONNECTION_PASSWORD_LENGTH ] = '\0';
        generateJSONValidString( peerConnectionContext.localCname,
                                 PEER_CONNECTION_CNAME_LENGTH );
        peerConnectionContext.localCname[ PEER_CONNECTION_CNAME_LENGTH ] = '\0';

        /* Generate answer cert in DER format */
        if( peerConnectionContext.dtlsContext.isInitialized == 0 )
        {
            /* Initialize DTLS session. */
            pDtlsSession = &pSession->dtlsSession;
            memset( pDtlsSession,
                    0,
                    sizeof( DtlsSession_t ) );

            /* pCtx->dtlsContext.isInitialized would be set to 1 in InitializeDtlsContext(). */
            ret = InitializeDtlsContext( &peerConnectionContext.dtlsContext );
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        memset( pSession, 0, sizeof( PeerConnectionSession_t ) );

        /* Initialize request queue. */
        ( void ) snprintf( tempName, sizeof( tempName ), "%s%02d", PEER_CONNECTION_MESSAGE_QUEUE_NAME, initSeq );

        /* Delete message queue from previous round. */
        MessageQueue_Destroy( NULL,
                              tempName );

        retMessageQueue = MessageQueue_Create( &pSession->requestQueue,
                                               tempName,
                                               sizeof( PeerConnectionSessionRequestMessage_t ),
                                               PEER_CONNECTION_MAX_QUEUE_MSG_NUM );
        if( retMessageQueue != MESSAGE_QUEUE_RESULT_OK )
        {
            LogError( ( "Fail to open message queue" ) );
            ret = PEER_CONNECTION_RESULT_FAIL_MQ_INIT;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Initialize session task. */
        ( void ) snprintf( tempName, sizeof( tempName ), "%s%02d", PEER_CONNECTION_SESSION_TASK_NAME, initSeq );

        if( pthread_create( &( pSession->pTaskHandler ),
                            NULL,
                            PeerConnection_SessionTask,
                            pSession ) != 0 )
        {
            LogError( ( "xTaskCreate(%s) failed", tempName ) );
            ret = PEER_CONNECTION_RESULT_FAIL_CREATE_TASK_ICE_CONTROLLER;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Initialize other modules. */
        ret = InitializeIceController( pSession );
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ( void ) snprintf( tempName, sizeof( tempName ), "%s%02d", PEER_CONNECTION_SESSION_RX_TASK_NAME, initSeq++ );

        if( pthread_create( &( pSession->pSocketListener ),
                            NULL,
                            IceControllerSocketListener_Task,
                            &( pSession->iceControllerContext ) ) != 0 )
        {
            LogError( ( "xTaskCreate(%s) failed", tempName ) );
            ret = PEER_CONNECTION_RESULT_FAIL_CREATE_TASK_ICE_SOCK_LISTENER;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSession->state = PEER_CONNECTION_SESSION_STATE_INITED;
        pSession->pCtx = &peerConnectionContext;
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_Start( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSession->state = PEER_CONNECTION_SESSION_STATE_START;
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_SetLocalDescription( PeerConnectionSession_t * pSession,
                                                           const PeerConnectionBufferSessionDescription_t * pBufferSessionDescription )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( ( pSession == NULL ) ||
        ( pBufferSessionDescription == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pBufferSessionDescription: %p",
                    pSession,
                    pBufferSessionDescription ) );
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* TODO: store configurations into session. */
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_SetRemoteDescription( PeerConnectionSession_t * pSession,
                                                            const PeerConnectionBufferSessionDescription_t * pBufferSessionDescription )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    IceControllerResult_t iceControllerResult;
    RtpResult_t resultRtp;
    RtcpResult_t resultRtcp;
    PeerConnectionBufferSessionDescription_t * pTargetRemoteSdp = NULL;

    if( ( pSession == NULL ) ||
        ( pBufferSessionDescription == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pBufferSessionDescription: %p", pSession, pBufferSessionDescription ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else if( ( pBufferSessionDescription->pSdpBuffer == NULL ) ||
             ( pBufferSessionDescription->sdpBufferLength > PEER_CONNECTION_SDP_DESCRIPTION_BUFFER_MAX_LENGTH ) )
    {
        LogError( ( "Invalid input, pBufferSessionDescription->pSdpBuffer: %p, pBufferSessionDescription->sdpBufferLength: %lu",
                    pBufferSessionDescription->pSdpBuffer, pBufferSessionDescription->sdpBufferLength ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {
        /* Empty else marker. */
    }

    /* Use SDP controller to parse SDP message into data structure. */
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pTargetRemoteSdp = &pSession->remoteSessionDescription;
        memset( pTargetRemoteSdp, 0, sizeof( PeerConnectionBufferSessionDescription_t ) );
        pTargetRemoteSdp->pSdpBuffer = pSession->remoteSdpBuffer;
        pTargetRemoteSdp->sdpBufferLength = pBufferSessionDescription->sdpBufferLength;
        pTargetRemoteSdp->type = pBufferSessionDescription->type;
        memcpy( pTargetRemoteSdp->pSdpBuffer, pBufferSessionDescription->pSdpBuffer, pTargetRemoteSdp->sdpBufferLength );

        ret = PeerConnectionSdp_DeserializeSdpMessage( pTargetRemoteSdp );
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Update codec information based on transceivers. */
        ret = PeerConnectionSdp_SetPayloadTypes( pSession, pTargetRemoteSdp );
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Validate deserialized SDP message. */
        if( ( pTargetRemoteSdp->sdpDescription.quickAccess.pIceUfrag == NULL ) ||
            ( pTargetRemoteSdp->sdpDescription.quickAccess.iceUfragLength + PEER_CONNECTION_USER_NAME_LENGTH > ( PEER_CONNECTION_USER_NAME_LENGTH << 1 ) ) )
        {
            LogWarn( ( "Remote user name is too long to store, length: %lu", pTargetRemoteSdp->sdpDescription.quickAccess.iceUfragLength ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_REMOTE_USERNAME;
        }
        else if( ( pTargetRemoteSdp->sdpDescription.quickAccess.pIceUfrag == NULL ) ||
                 ( pTargetRemoteSdp->sdpDescription.quickAccess.icePwdLength > PEER_CONNECTION_PASSWORD_LENGTH ) )
        {
            LogWarn( ( "Remote user password is too long to store, length: %lu", pTargetRemoteSdp->sdpDescription.quickAccess.icePwdLength ) );
            ret = ICE_CONTROLLER_RESULT_INVALID_REMOTE_PASSWORD;
        }
        else
        {
            /* Empty else marker. */
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        memcpy( pSession->remoteUserName,
                pTargetRemoteSdp->sdpDescription.quickAccess.pIceUfrag,
                pTargetRemoteSdp->sdpDescription.quickAccess.iceUfragLength );
        pSession->remoteUserName[ pTargetRemoteSdp->sdpDescription.quickAccess.iceUfragLength ] = '\0';
        memcpy( pSession->remotePassword,
                pTargetRemoteSdp->sdpDescription.quickAccess.pIcePwd,
                pTargetRemoteSdp->sdpDescription.quickAccess.icePwdLength );
        pSession->remotePassword[ pTargetRemoteSdp->sdpDescription.quickAccess.icePwdLength ] = '\0';
        snprintf( pSession->combinedName,
                  ( PEER_CONNECTION_USER_NAME_LENGTH << 1 ) + 2,
                  "%.*s:%.*s",
                  ( int ) pTargetRemoteSdp->sdpDescription.quickAccess.iceUfragLength,
                  pSession->remoteUserName,
                  PEER_CONNECTION_USER_NAME_LENGTH,
                  peerConnectionContext.localUserName );
        memcpy( pSession->remoteCertFingerprint,
                pTargetRemoteSdp->sdpDescription.quickAccess.pFingerprint,
                pTargetRemoteSdp->sdpDescription.quickAccess.fingerprintLength );
        pSession->remoteCertFingerprint[ pTargetRemoteSdp->sdpDescription.quickAccess.fingerprintLength ] = '\0';
        pSession->remoteCertFingerprintLength = pTargetRemoteSdp->sdpDescription.quickAccess.fingerprintLength;

        iceControllerResult = IceController_Start( &pSession->iceControllerContext,
                                                   peerConnectionContext.localUserName,
                                                   PEER_CONNECTION_USER_NAME_LENGTH,
                                                   peerConnectionContext.localPassword,
                                                   PEER_CONNECTION_PASSWORD_LENGTH,
                                                   pSession->remoteUserName,
                                                   pTargetRemoteSdp->sdpDescription.quickAccess.iceUfragLength,
                                                   pSession->remotePassword,
                                                   pTargetRemoteSdp->sdpDescription.quickAccess.icePwdLength,
                                                   pSession->combinedName,
                                                   strlen( pSession->combinedName ) );
        if( iceControllerResult != ICE_CONTROLLER_RESULT_OK )
        {
            LogWarn( ( "IceController_Start fail, result: %d.", iceControllerResult ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_START;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultRtp = Rtp_Init( &peerConnectionContext.rtpContext );
        if( resultRtp != RTP_RESULT_OK )
        {
            LogError( ( "Fail to initialize RTP context, result: %d", resultRtp ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTP_INIT;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        resultRtcp = Rtcp_Init( &peerConnectionContext.rtcpContext );
        if( resultRtcp != RTCP_RESULT_OK )
        {
            LogError( ( "Fail to initialize RTCP context, result: %d", resultRtcp ) );
            ret = PEER_CONNECTION_RESULT_FAIL_RTCP_INIT;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        if( pTargetRemoteSdp->sdpDescription.quickAccess.pRemoteCandidate != NULL )
        {
            LogInfo( ( "Add remote candidate in SDP offer/answer(%lu): %.*s",
                       pTargetRemoteSdp->sdpDescription.quickAccess.remoteCandidateLength,
                       ( int ) pTargetRemoteSdp->sdpDescription.quickAccess.remoteCandidateLength,
                       pTargetRemoteSdp->sdpDescription.quickAccess.pRemoteCandidate ) );
            ret = PeerConnection_AddRemoteCandidate( pSession,
                                                     pTargetRemoteSdp->sdpDescription.quickAccess.pRemoteCandidate,
                                                     pTargetRemoteSdp->sdpDescription.quickAccess.remoteCandidateLength );
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSession->rtpConfig.videoRtxSequenceNumber = 0U;
        pSession->rtpConfig.audioRtxSequenceNumber = 0U;
        pSession->rtpConfig.twccId = ( uint16_t ) pTargetRemoteSdp->sdpDescription.quickAccess.twccExtId;
        pSession->rtpConfig.remoteVideoSsrc = pTargetRemoteSdp->sdpDescription.quickAccess.videoSsrc;
        pSession->rtpConfig.remoteAudioSsrc = pTargetRemoteSdp->sdpDescription.quickAccess.audioSsrc;

        pSession->state = PEER_CONNECTION_SESSION_STATE_START;
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_SetVideoOnFrame( PeerConnectionSession_t * pSession,
                                                       OnFrameReadyCallback_t onFrameReadyCallbackFunc,
                                                       void * pOnFrameReadyCallbackCustomContext )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSession->videoSrtpReceiver.onFrameReadyCallbackFunc = onFrameReadyCallbackFunc;
        pSession->videoSrtpReceiver.pOnFrameReadyCallbackCustomContext = pOnFrameReadyCallbackCustomContext;
    }

    return ret;
}
PeerConnectionResult_t PeerConnection_SetAudioOnFrame( PeerConnectionSession_t * pSession,
                                                       OnFrameReadyCallback_t onFrameReadyCallbackFunc,
                                                       void * pOnFrameReadyCallbackCustomContext )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSession->audioSrtpReceiver.onFrameReadyCallbackFunc = onFrameReadyCallbackFunc;
        pSession->audioSrtpReceiver.pOnFrameReadyCallbackCustomContext = pOnFrameReadyCallbackCustomContext;
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_AddRemoteCandidate( PeerConnectionSession_t * pSession,
                                                          const char * pDecodeMessage,
                                                          size_t decodeMessageLength )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    IceControllerResult_t iceControllerResult;
    IceControllerCandidate_t candidate;

    if( ( pSession == NULL ) ||
        ( pDecodeMessage == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pDecodeMessage: %p",
                    pSession, pDecodeMessage ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        iceControllerResult = IceController_DeserializeIceCandidate( pDecodeMessage, decodeMessageLength, &candidate );
        if( iceControllerResult != ICE_CONTROLLER_RESULT_OK )
        {
            LogWarn( ( "IceController_DeserializeIceCandidate fail, result: %d, dropping ICE candidate.", iceControllerResult ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_DESERIALIZE_CANDIDATE;
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = SendRemoteCandidateRequest( pSession,
                                          &candidate );
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_AddTransceiver( PeerConnectionSession_t * pSession,
                                                      Transceiver_t * pTransceiver )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( pSession == NULL )
    {
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = AllocateTransceiver( pSession, pTransceiver );
    }

    return ret;
}

#if ENABLE_SCTP_DATA_CHANNEL
PeerConnectionResult_t PeerConnection_AddDataChannel( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    pSession->ucEnableDataChannelLocal = 1;
    return ret;
}
#endif /* ENABLE_SCTP_DATA_CHANNEL */

PeerConnectionResult_t PeerConnection_MatchTransceiverBySsrc( PeerConnectionSession_t * pSession,
                                                              uint32_t ssrc,
                                                              const Transceiver_t ** ppTransceiver )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    int i;

    if( ( pSession == NULL ) || ( ppTransceiver == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, ppTransceiver: %p", pSession, ppTransceiver ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        *ppTransceiver = NULL;
        for( i = 0; i < pSession->transceiverCount; i++ )
        {
            if( ssrc == pSession->pTransceivers[i]->ssrc )
            {
                *ppTransceiver = pSession->pTransceivers[i];
                break;
            }
        }

        if( i == pSession->transceiverCount )
        {
            LogWarn( ( "No transceiver for SSRC: %u", ssrc ) );
            ret = PEER_CONNECTION_RESULT_UNKNOWN_SSRC;
        }
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_CloseSession( PeerConnectionSession_t * pSession )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    int i;

    if( pSession == NULL )
    {
        LogError( ( "Invalid input, pSession: %p", pSession ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    /* Update session state and notify transceivers. */
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSession->state = PEER_CONNECTION_SESSION_STATE_CLOSING;

        for( i = 0; i < pSession->transceiverCount; i++ )
        {
            if( pSession->pTransceivers[i]->onPcEventCallbackFunc )
            {
                pSession->pTransceivers[i]->onPcEventCallbackFunc( pSession->pTransceivers[i]->pOnPcEventCustomContext,
                                                                   TRANSCEIVER_CB_EVENT_REMOTE_PEER_CLOSED,
                                                                   NULL );
            }
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        #if ENABLE_SCTP_DATA_CHANNEL
        PeerConnectionResult_t xSCTPRet;
        /* Close and deallocate all data channels along with terminating
         * SCTP session. */
        xSCTPRet = PeerConnectionSCTP_DeallocateSCTP( pSession );
        if( xSCTPRet == PEER_CONNECTION_RESULT_OK )
        {
            LogDebug( ( "Closed SCTP session. \r\n" ) );
        }
        else
        {
            LogError( ( "Fail to close SCTP session, result: %d", xSCTPRet ) );
            ret = PEER_CONNECTION_RESULT_FAIL_SCTP_CLOSE;
        }
        #endif /* #if ENABLE_SCTP_DATA_CHANNEL */
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = DestroyIceController( pSession );
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = PeerConnectionSrtp_DeInit( pSession );
        if( ret != PEER_CONNECTION_RESULT_OK )
        {
            LogError( ( "PeerConnectionSrtp_DeInit fail, result: %d", ret ) );
        }
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        // Free each transceiver in the array
        memset( pSession->pTransceivers, 0, sizeof( pSession->pTransceivers ) );
        pSession->transceiverCount = 0;
        pSession->mLinesTransceiverCount = 0;

        /* Reset the state to inited for user to re-use. */
        pSession->state = PEER_CONNECTION_SESSION_STATE_INITED;
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_WriteFrame( PeerConnectionSession_t * pSession,
                                                  Transceiver_t * pTransceiver,
                                                  const PeerConnectionFrame_t * pFrame )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( ( pSession == NULL ) ||
        ( pTransceiver == NULL ) ||
        ( pFrame == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pTransceiver: %p, pFrame: %p",
                    pSession, pTransceiver, pFrame ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    /* Encode the frame into multiple payload buffers (>=1). */
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        if( pSession->state < PEER_CONNECTION_SESSION_STATE_CONNECTION_READY )
        {
            LogDebug( ( "This session is not ready for sending frames." ) );
        }
        else if( TRANSCEIVER_IS_CODEC_ENABLED( pTransceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_BIT ) )
        {
            ret = PeerConnectionSrtp_WriteH264Frame( pSession, pTransceiver, pFrame );
        }
        else if( TRANSCEIVER_IS_CODEC_ENABLED( pTransceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_OPUS_BIT ) )
        {
            ret = PeerConnectionSrtp_WriteOpusFrame( pSession, pTransceiver, pFrame );
        }
        else if( TRANSCEIVER_IS_CODEC_ENABLED( pTransceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_VP8_BIT ) )
        {

        }
        else if( TRANSCEIVER_IS_CODEC_ENABLED( pTransceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_MULAW_BIT ) )
        {
            ret = PeerConnectionSrtp_WriteG711Frame( pSession, pTransceiver, pFrame );
        }
        else if( TRANSCEIVER_IS_CODEC_ENABLED( pTransceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_ALAW_BIT ) )
        {
            ret = PeerConnectionSrtp_WriteG711Frame( pSession, pTransceiver, pFrame );
        }
        else if( TRANSCEIVER_IS_CODEC_ENABLED( pTransceiver->codecBitMap, TRANSCEIVER_RTC_CODEC_H265_BIT ) )
        {

        }
        else
        {
            /* TODO: Unknown, no matching codec. */
            LogError( ( "Codec is not supported, codec bit map: 0x%x", ( int ) pTransceiver->codecBitMap ) );
            ret = PEER_CONNECTION_RESULT_UNKNOWN_TX_CODEC;
        }
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_CreateOffer( PeerConnectionSession_t * pSession,
                                                   PeerConnectionBufferSessionDescription_t * pOutputBufferSessionDescription,
                                                   char * pOutputSerializedSdpMessage,
                                                   size_t * pOutputSerializedSdpMessageLength )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( ( pSession == NULL ) ||
        ( pOutputBufferSessionDescription == NULL ) ||
        ( pOutputSerializedSdpMessage == NULL ) ||
        ( pOutputSerializedSdpMessageLength == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pOutputBufferSessionDescription: %p, pOutputSerializedSdpMessage: %p, pOutputSerializedSdpMessageLength: %p",
                    pSession, pOutputBufferSessionDescription, pOutputSerializedSdpMessage, pOutputSerializedSdpMessageLength ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else if( pOutputBufferSessionDescription->pSdpBuffer == NULL )
    {
        LogError( ( "Invalid input, pOutputBufferSessionDescription->pSdpBuffer: %p", pOutputBufferSessionDescription->pSdpBuffer ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {
        /* Empty else marker. */
    }

    /* Use the default codec while creating SDP offer. */
    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        /* Update codec information based on transceivers. */
        ret = SetDefaultPayloadTypes( pSession );
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = PeerConnectionSdp_PopulateSessionDescription( pSession,
                                                            NULL,
                                                            pOutputBufferSessionDescription,
                                                            pOutputSerializedSdpMessage,
                                                            pOutputSerializedSdpMessageLength );
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_CreateAnswer( PeerConnectionSession_t * pSession,
                                                    PeerConnectionBufferSessionDescription_t * pOutputBufferSessionDescription,
                                                    char * pOutputSerializedSdpMessage,
                                                    size_t * pOutputSerializedSdpMessageLength )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( ( pSession == NULL ) ||
        ( pOutputBufferSessionDescription == NULL ) ||
        ( pOutputSerializedSdpMessage == NULL ) ||
        ( pOutputSerializedSdpMessageLength == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pOutputBufferSessionDescription: %p, pOutputSerializedSdpMessage: %p, pOutputSerializedSdpMessageLength: %p",
                    pSession, pOutputBufferSessionDescription, pOutputSerializedSdpMessage, pOutputSerializedSdpMessageLength ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else if( pOutputBufferSessionDescription->pSdpBuffer == NULL )
    {
        LogError( ( "Invalid input, pOutputBufferSessionDescription->pSdpBuffer: %p", pOutputBufferSessionDescription->pSdpBuffer ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }
    else
    {
        /* Empty else marker. */
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        ret = PeerConnectionSdp_PopulateSessionDescription( pSession,
                                                            &pSession->remoteSessionDescription,
                                                            pOutputBufferSessionDescription,
                                                            pOutputSerializedSdpMessage,
                                                            pOutputSerializedSdpMessageLength );
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_SetOnLocalCandidateReady( PeerConnectionSession_t * pSession,
                                                                OnIceCandidateReadyCallback_t onLocalCandidateReadyCallbackFunc,
                                                                void * pOnLocalCandidateReadyCallbackCustomContext )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;

    if( ( pSession == NULL ) ||
        ( onLocalCandidateReadyCallbackFunc == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, onLocalCandidateReadyCallbackFunc: %p",
                    pSession, onLocalCandidateReadyCallbackFunc ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        pSession->onIceCandidateReadyCallbackFunc = onLocalCandidateReadyCallbackFunc;
        pSession->pOnLocalCandidateReadyCallbackCustomContext = pOnLocalCandidateReadyCallbackCustomContext;
    }

    return ret;
}

PeerConnectionResult_t PeerConnection_AddIceServerConfig( PeerConnectionSession_t * pSession,
                                                          IceControllerIceServer_t * pIceServers,
                                                          size_t iceServersCount )
{
    PeerConnectionResult_t ret = PEER_CONNECTION_RESULT_OK;
    IceControllerResult_t iceControllerResult;

    if( ( pSession == NULL ) ||
        ( pIceServers == NULL ) )
    {
        LogError( ( "Invalid input, pSession: %p, pIceServers: %p",
                    pSession, pIceServers ) );
        ret = PEER_CONNECTION_RESULT_BAD_PARAMETER;
    }

    if( ret == PEER_CONNECTION_RESULT_OK )
    {
        iceControllerResult = IceController_AddIceServerConfig( &pSession->iceControllerContext,
                                                                pIceServers,
                                                                iceServersCount );
        if( iceControllerResult != ICE_CONTROLLER_RESULT_OK )
        {
            LogError( ( "Fail to add Ice server config into Ice Controller." ) );
            ret = PEER_CONNECTION_RESULT_FAIL_ICE_CONTROLLER_ADD_ICE_SERVER_CONFIG;
        }
    }

    return ret;
}
