# Option for GStreamer master example
option(BUILD_GSTREAMER_MASTER "Enable building GStreamer master example" ON)

if(BUILD_GSTREAMER_MASTER)
    # Find required packages
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GST REQUIRED
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
        gstreamer-audio-1.0)

    # Add the GStreamer master executable
    add_executable(WebRTCLinuxApplicationGstMaster
        ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_SOURCE_FILES}
        ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_SOURCE_FILES}
        ${WEBRTC_APPLICATION_NETWORKING_UTILS_SOURCE_FILES}
        ${WEBRTC_APPLICATION_UTILS_SOURCE_FILES}
        ${WEBRTC_APPLICATION_SDP_CONTROLLER_SOURCE_FILES}
        ${WEBRTC_APPLICATION_ICE_CONTROLLER_SOURCE_FILES}
        ${WEBRTC_APPLICATION_MBEDTLS_SOURCE_FILES}
        ${WEBRTC_APPLICATION_COREHTTP_SOURCE_FILES}
        ${WEBRTC_APPLICATION_LIBSRTP_SOURCE_FILES}
        ${RTCP_SOURCES}
        ${RTP_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/gst_master/gst_master.c")

    # Add include directories
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${WEBRTC_APPLICATION_MASTER_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_NETWORKING_UTILS_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_UTILS_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_SDP_CONTROLLER_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_ICE_CONTROLLER_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_MBEDTLS_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_COREHTTP_INCLUDE_DIRS}
        ${WEBRTC_APPLICATION_LIBSRTP_INCLUDE_DIRS}
        ${RTCP_INCLUDE_PUBLIC_DIRS}
        ${RTP_INCLUDE_PUBLIC_DIRS}
        ${GST_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/master
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/gst_master
        ${CMAKE_CURRENT_SOURCE_DIR}/configs)

    # Include dependencies
    ## Include sigV4
    message(STATUS "including sigv4 directories: ${SIGV4_INCLUDE_PUBLIC_DIRS}")
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${SIGV4_INCLUDE_PUBLIC_DIRS})

    ## Include libwebsockets
    message(STATUS "including libwebsockets directories: ${LIBWEBSOCKETS_INCLUDE_DIRS}")
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${LIBWEBSOCKETS_INCLUDE_DIRS})

    ## Include coreJSON
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${JSON_INCLUDE_PUBLIC_DIRS})

    ## Include signaling
    target_compile_definitions(WebRTCLinuxApplicationGstMaster PUBLIC
        SIGNALING_DO_NOT_USE_CUSTOM_CONFIG
        MBEDTLS_CONFIG_FILE="mbedtls_custom_config.h"
        HTTP_DO_NOT_USE_CUSTOM_CONFIG
        HAVE_CONFIG_H)

    message(STATUS "including signaling directories: ${SIGNALING_INCLUDE_PUBLIC_DIRS}")
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${SIGNALING_INCLUDE_PUBLIC_DIRS})

    ## Include SDP
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${SDP_INCLUDE_PUBLIC_DIRS})

    ## Include STUN
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${STUN_INCLUDE_PUBLIC_DIRS})

    ## Include ICE
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${ICE_INCLUDE_PUBLIC_DIRS})

    if(BUILD_USRSCTP_LIBRARY)
        target_compile_definitions(WebRTCLinuxApplicationGstMaster PRIVATE
            ENABLE_SCTP_DATA_CHANNEL=1)

        ## Set DCEP include directories
        target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
            ${DCEP_INCLUDE_PUBLIC_DIRS})
    endif()

    # usrsctp library public include directories
    set(SCTP_INCLUDE_PUBLIC_DIRS
        "${CMAKE_ROOT_DIRECTORY}/libraries/protocols/usrsctp/usrsctplib")
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${SCTP_INCLUDE_PUBLIC_DIRS})

    # Link libraries
    target_link_libraries(WebRTCLinuxApplicationGstMaster
        websockets
        sigv4
        signaling
        corejson
        sdp
        ice
        rt
        pthread
        ${GST_LIBRARIES})

    if(BUILD_USRSCTP_LIBRARY)
        target_link_libraries(WebRTCLinuxApplicationGstMaster
            usrsctp
            dcep)
    endif()

    # Add compile options
    target_compile_options(WebRTCLinuxApplicationGstMaster PRIVATE
        -Wall
        -Werror
        ${GST_CFLAGS_OTHER})

    # Add compile definitions
    target_compile_definitions(WebRTCLinuxApplicationGstMaster PRIVATE
        ENABLE_SCTP_DATA_CHANNEL=1)

endif()
