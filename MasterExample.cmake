file(
  GLOB
  WEBRTC_APPLICATION_MASTER_SOURCE_FILES
  "examples/master/*.c"
  "examples/app_common/*.c" )

set( WEBRTC_APPLICATION_MASTER_INCLUDE_DIRS
     "examples/master"
     "examples/app_common" )

add_executable(
    WebRTCLinuxApplicationMaster
    ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_SOURCE_FILES}
    ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_SOURCE_FILES}
    ${WEBRTC_APPLICATION_NETWORKING_UTILS_SOURCE_FILES}
    ${WEBRTC_APPLICATION_MASTER_SOURCE_FILES}
    ${WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES}
    ${WEBRTC_APPLICATION_MASTER_MEDIA_SOURCE_FILES}
    ${WEBRTC_APPLICATION_SDP_CONTROLLER_SOURCE_FILES}
    ${WEBRTC_APPLICATION_ICE_CONTROLLER_SOURCE_FILES}
    ${WEBRTC_APPLICATION_MBEDTLS_SOURCE_FILES}
    ${WEBRTC_APPLICATION_COREHTTP_SOURCE_FILES}
    ${WEBRTC_APPLICATION_LIBSRTP_SOURCE_FILES} )

target_include_directories( WebRTCLinuxApplicationMaster PRIVATE
                            ${WEBRTC_APPLICATION_MASTER_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_NETWORKING_UTILS_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_MASTER_MEDIA_INCLUDE_FILES}
                            ${WEBRTC_APPLICATION_SDP_CONTROLLER_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_ICE_CONTROLLER_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_MBEDTLS_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_COREHTTP_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_LIBSRTP_INCLUDE_DIRS} )

## Set libwebsockets include directories
message(STATUS "including libwebsockets directories: ${LIBWEBSOCKETS_INCLUDE_DIRS}")
target_include_directories( WebRTCLinuxApplicationMaster PRIVATE
                            ${LIBWEBSOCKETS_INCLUDE_DIRS} )

## Set signaling include directories
target_compile_definitions( WebRTCLinuxApplicationMaster
                            PUBLIC
                            MBEDTLS_CONFIG_FILE="mbedtls_custom_config.h" )

if( BUILD_USRSCTP_LIBRARY )
    ## Include usrsctp
    target_compile_definitions( WebRTCLinuxApplicationMaster PRIVATE ENABLE_SCTP_DATA_CHANNEL=1 )
else()
    target_compile_definitions( WebRTCLinuxApplicationMaster PRIVATE ENABLE_SCTP_DATA_CHANNEL=0 )
endif()

if( METRIC_PRINT_ENABLED )
    ## Include metrics logging
    target_compile_definitions( WebRTCLinuxApplicationMaster PRIVATE METRIC_PRINT_ENABLED=1 )
else()
    target_compile_definitions( WebRTCLinuxApplicationMaster PRIVATE METRIC_PRINT_ENABLED=0 )
endif()

if( ENABLE_MEDIA_LOOPBACK )
    target_compile_definitions( WebRTCLinuxApplicationMaster PRIVATE ENABLE_STREAMING_LOOPBACK )
endif()

# link application with dependencies, note that rt is librt providing message queue's APIs
message(STATUS "linking websockets to WebRTCLinuxApplication")
target_link_libraries( WebRTCLinuxApplicationMaster
                       websockets
                       sigv4
                       signaling
                       corejson
                       sdp
                       ice
                       rtcp
                       rtp
                       stun
                       libsrtp
                       rt
                       pthread
)

if( BUILD_USRSCTP_LIBRARY )
    target_link_libraries( WebRTCLinuxApplicationMaster
                           usrsctp
                           dcep )
endif()

target_compile_options( WebRTCLinuxApplicationMaster PRIVATE -Wall -Werror )
