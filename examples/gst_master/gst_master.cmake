# gst-master.cmake

# Option for GStreamer master example
option(BUILD_GSTREAMER_MASTER "Enable building GStreamer master example" ON)

if(BUILD_GSTREAMER_MASTER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GST REQUIRED
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
        gstreamer-audio-1.0)

    if(GST_FOUND)
        message(STATUS "GStreamer found")
    else()
        message(FATAL_ERROR "GStreamer not found")
    endif()

    file(
        GLOB
        WEBRTC_APPLICATION_GST_MASTER_SOURCE_FILES
        "examples/gst_master/*.c")

    set(WEBRTC_APPLICATION_GST_MASTER_INCLUDE_DIRS
        "examples/gst_master/"
        ${GST_INCLUDE_DIRS})

    add_executable(
        WebRTCLinuxApplicationGstMaster
        ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_SOURCE_FILES}
        ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_SOURCE_FILES}
        ${WEBRTC_APPLICATION_NETWORKING_UTILS_SOURCE_FILES}
        ${WEBRTC_APPLICATION_GST_MASTER_SOURCE_FILES}
        ${WEBRTC_APPLICATION_UTILS_SOURCE_FILES}
        ${WEBRTC_APPLICATION_SDP_CONTROLLER_SOURCE_FILES}
        ${WEBRTC_APPLICATION_ICE_CONTROLLER_SOURCE_FILES}
        ${WEBRTC_APPLICATION_MBEDTLS_SOURCE_FILES}
        ${WEBRTC_APPLICATION_COREHTTP_SOURCE_FILES}
        ${WEBRTC_APPLICATION_LIBSRTP_SOURCE_FILES}
        ${RTCP_SOURCES}
        ${RTP_SOURCES})

    # Add master include directories
    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${WEBRTC_APPLICATION_GST_MASTER_INCLUDE_DIRS}
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
        "configs/mbedtls"
        "configs/sigv4")

    target_compile_definitions(WebRTCLinuxApplicationGstMaster
        PUBLIC
        SIGNALING_DO_NOT_USE_CUSTOM_CONFIG
        MBEDTLS_CONFIG_FILE="mbedtls_custom_config.h"
        HTTP_DO_NOT_USE_CUSTOM_CONFIG
        HAVE_CONFIG_H)

    # Include all dependencies
    ## Include sigV4
    if(NOT TARGET sigv4)
        include(${CMAKE_ROOT_DIRECTORY}/CMake/sigV4.cmake)
    endif()

    ## Include libwebsockets
    if(NOT TARGET websockets)
        include(${CMAKE_ROOT_DIRECTORY}/CMake/libwebsockets.cmake)
    endif()

    ## Include coreJSON
    if(NOT TARGET corejson)
        include(${CMAKE_ROOT_DIRECTORY}/CMake/coreJSON.cmake)
    endif()

    ## Include signaling
    if(NOT TARGET signaling)
        include(${CMAKE_ROOT_DIRECTORY}/CMake/signaling.cmake)
    endif()

    ## Include SDP
    if(NOT TARGET sdp)
        include(${CMAKE_ROOT_DIRECTORY}/CMake/sdp.cmake)
    endif()

    ## Include STUN
    if(NOT TARGET stun)
        include(${CMAKE_ROOT_DIRECTORY}/CMake/stun.cmake)
    endif()

    ## Include ICE
    if(NOT TARGET ice)
        include(${CMAKE_ROOT_DIRECTORY}/CMake/ice.cmake)
    endif()

    target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
        ${SIGV4_INCLUDE_PUBLIC_DIRS}
        ${LIBWEBSOCKETS_INCLUDE_DIRS}
        ${JSON_INCLUDE_PUBLIC_DIRS}
        ${SIGNALING_INCLUDE_PUBLIC_DIRS}
        ${SDP_INCLUDE_PUBLIC_DIRS}
        ${STUN_INCLUDE_PUBLIC_DIRS}
        ${ICE_INCLUDE_PUBLIC_DIRS}
        ${SCTP_INCLUDE_PUBLIC_DIRS})

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
        if(NOT TARGET usrsctp)
            include(${CMAKE_ROOT_DIRECTORY}/CMake/libusrsctp.cmake)
        endif()
        if(NOT TARGET dcep)
            include(${CMAKE_ROOT_DIRECTORY}/CMake/dcep.cmake)
        endif()

        # Add DCEP include directories
        target_include_directories(WebRTCLinuxApplicationGstMaster PRIVATE
            ${DCEP_INCLUDE_PUBLIC_DIRS}
            "examples/libusrsctp")  # Add this line to include sctp_utils.h directory

        target_link_libraries(WebRTCLinuxApplicationGstMaster
            usrsctp
            dcep)
        target_compile_definitions(WebRTCLinuxApplicationGstMaster PRIVATE ENABLE_SCTP_DATA_CHANNEL=1)
    endif()

    target_compile_options(WebRTCLinuxApplicationGstMaster PRIVATE
        -Wall
        -Werror
        ${GST_CFLAGS_OTHER})
endif()
