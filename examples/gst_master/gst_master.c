/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "demo_config.h"
#include "app_common.h"
#include "gst_media_source.h"
#include <signal.h>

AppContext_t appContext;
GstMediaSourcesContext_t gstMediaSourceContext;

static void SignalHandler( int signum );
static int32_t InitTransceiver( void * pMediaCtx, TransceiverTrackKind_t trackKind, Transceiver_t * pTranceiver );
static int32_t OnMediaSinkHook( void * pCustom,
                                WebrtcFrame_t * pFrame );
static int32_t InitializeGstMediaSource( AppContext_t * pAppContext,
                                         GstMediaSourcesContext_t * pGstMediaSourceContext );


static void SignalHandler( int signum )
{
    int32_t ret = 0;

    if( signum == SIGINT )
    {
        LogInfo( ( "Received SIGINT, initiating cleanup..." ) );
        ret = GstMediaSource_Cleanup( &gstMediaSourceContext );
    }

    if( ret != 0 )
    {
        LogError( ( "Failed to clean up resources" ) );
    }
    else
    {
        LogInfo( ( "Cleanup completed successfully" ) );
    }

    exit( ret );
}

static int32_t InitTransceiver( void * pMediaCtx, TransceiverTrackKind_t trackKind, Transceiver_t * pTranceiver )
{
    int32_t ret = 0;
    GstMediaSourcesContext_t * pMediaSourceContext = ( GstMediaSourcesContext_t * )pMediaCtx;

    if( ( pMediaCtx == NULL ) || ( pTranceiver == NULL ) )
    {
        LogError( ( "Invalid input, pMediaCtx: %p, pTranceiver: %p", pMediaCtx, pTranceiver ) );
        ret = -1;
    }
    else if( ( trackKind != TRANSCEIVER_TRACK_KIND_VIDEO ) &&
             ( trackKind != TRANSCEIVER_TRACK_KIND_AUDIO ) )
    {
        LogError( ( "Invalid track kind: %d", trackKind ) );
        ret = -2;
    }
    else
    {
        /* Empty else marker. */
    }

    if( ret == 0 )
    {
        switch( trackKind )
        {
            case TRANSCEIVER_TRACK_KIND_VIDEO:
                ret = GstMediaSource_InitVideoTransceiver( pMediaSourceContext,
                                                           pTranceiver );
                break;
            case TRANSCEIVER_TRACK_KIND_AUDIO:
                ret = GstMediaSource_InitAudioTransceiver( pMediaSourceContext,
                                                           pTranceiver );
                break;
            default:
                LogError( ( "Invalid track kind: %d", trackKind ) );
                ret = -3;
                break;
        }
    }

    return ret;
}

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

static int32_t InitializeGstMediaSource( AppContext_t * pAppContext,
                                         GstMediaSourcesContext_t * pGstMediaSourceContext )
{
    int32_t ret = 0;

    if( ( pAppContext == NULL ) ||
        ( pGstMediaSourceContext == NULL ) )
    {
        LogError( ( "Invalid input, pAppContext: %p, pGstMediaSourceContext: %p", pAppContext, pGstMediaSourceContext ) );
        ret = -1;
    }

    if (ret == 0)
    {
        ret = GstMediaSource_Init( pGstMediaSourceContext,
                                   OnMediaSinkHook,
                                   pAppContext );
    }

    return ret;
}

int main( void )
{
    int ret = 0;
    struct sigaction sa;

    // Set up signal handling
    sa.sa_handler = SignalHandler;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = 0;
    if ( sigaction( SIGINT, &sa, NULL ) == -1 )
    {
        LogError( ( "Failed to set up signal handler" ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        ret = AppCommon_Init( &appContext, InitTransceiver, &gstMediaSourceContext );
    }

    if( ret == 0 )
    {
        ret = InitializeGstMediaSource( &appContext, &gstMediaSourceContext );
    }

    if( ret == 0 )
    {
        /* Configure signaling controller with client ID and role type. */
        memcpy( &( appContext.signalingControllerClientId[ 0 ] ), SIGNALING_CONTROLLER_MASTER_CLIENT_ID, SIGNALING_CONTROLLER_MASTER_CLIENT_ID_LENGTH );
        appContext.signalingControllerClientId[ SIGNALING_CONTROLLER_MASTER_CLIENT_ID_LENGTH ] = '\0';
        appContext.signalingControllerClientIdLength = SIGNALING_CONTROLLER_MASTER_CLIENT_ID_LENGTH;
        appContext.signalingControllerRole = SIGNALING_ROLE_MASTER;
    }

    if( ret == 0 )
    {
        /* Launch application with current thread serving as Signaling Controller. */
        ret = AppCommon_StartSignalingController( &appContext );
    }

    if( ret == 0 )
    {
        /* Launch application with current thread serving as Signaling Controller. */
        AppCommon_WaitSignalingControllerStop( &appContext );
    }

    if( ret == 0 )
    {
        ret = GstMediaSource_Cleanup( &gstMediaSourceContext );
    }

    return ret;
}
