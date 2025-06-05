# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules( GST
                   gstreamer-1.0
                   gstreamer-app-1.0
                   gstreamer-video-1.0
                   gstreamer-audio-1.0)

if(GST_FOUND)
    file( GLOB
          WEBRTC_APPLICATION_GST_MASTER_SOURCE_FILES
          "examples/gst_master/*.c"
          "examples/app_common/*.c" )

    set( WEBRTC_APPLICATION_GST_MASTER_INCLUDE_DIRS
         "examples/gst_master"
         "examples/app_common" )

    file( GLOB
          WEBRTC_APPLICATION_GST_MASTER_MEDIA_SOURCE_FILES
          "examples/gst_media_source/*.c" )

    set( WEBRTC_APPLICATION_GST_MASTER_MEDIA_INCLUDE_FILES
         "examples/gst_media_source" )

    # Add the GStreamer master executable
    add_executable( WebRTCLinuxApplicationGstMaster
                    ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_NETWORKING_UTILS_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_SDP_CONTROLLER_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_ICE_CONTROLLER_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_MBEDTLS_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_COREHTTP_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_LIBSRTP_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_GST_MASTER_SOURCE_FILES}
                    ${WEBRTC_APPLICATION_GST_MASTER_MEDIA_SOURCE_FILES})

    # Add include directories
    target_include_directories( WebRTCLinuxApplicationGstMaster PRIVATE
                                ${WEBRTC_APPLICATION_MASTER_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_NETWORKING_UTILS_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_SDP_CONTROLLER_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_ICE_CONTROLLER_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_MBEDTLS_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_COREHTTP_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_LIBSRTP_INCLUDE_DIRS}
                                ${GST_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_GST_MASTER_INCLUDE_DIRS}
                                ${WEBRTC_APPLICATION_GST_MASTER_MEDIA_INCLUDE_FILES})

    ## Set signaling include directories
    target_compile_definitions( WebRTCLinuxApplicationGstMaster
                                PUBLIC
                                MBEDTLS_CONFIG_FILE="mbedtls_custom_config.h" )

    if( BUILD_USRSCTP_LIBRARY )
        ## Include usrsctp
        target_compile_definitions( WebRTCLinuxApplicationGstMaster PRIVATE ENABLE_SCTP_DATA_CHANNEL=1 )
    else()
        target_compile_definitions( WebRTCLinuxApplicationGstMaster PRIVATE ENABLE_SCTP_DATA_CHANNEL=0 )
    endif()

    if( METRIC_PRINT_ENABLED )
        ## Include metrics logging
        target_compile_definitions( WebRTCLinuxApplicationGstMaster PRIVATE METRIC_PRINT_ENABLED=1 )
    else()
        target_compile_definitions( WebRTCLinuxApplicationGstMaster PRIVATE METRIC_PRINT_ENABLED=0 )
    endif()

    target_link_libraries( WebRTCLinuxApplicationGstMaster
                           sigv4
                           signaling
                           corejson
                           sdp
                           ice
                           rtcp
                           rtp
                           stun
                           mbedtls
                           libsrtp
                           websockets
                           rt
                           pthread
                           ${GST_LIBRARIES} )

    if( BUILD_USRSCTP_LIBRARY )
        target_link_libraries( WebRTCLinuxApplicationGstMaster
                               usrsctp
                               dcep)
    endif()

    target_compile_options( WebRTCLinuxApplicationGstMaster PRIVATE -Wall -Werror )

else() # GST_FOUND
    message(WARNING "GStreamer packages not found - GStreamer applications will not be built")
endif() # GST_FOUND