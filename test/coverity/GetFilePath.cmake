file(
  GLOB
  WEBRTC_APPLICATION_SIGNALING_CONTROLLER_SOURCE_FILES
  "${MODULE_ROOT_DIR}/examples/signaling_controller/*.c" )

set( WEBRTC_APPLICATION_SIGNALING_CONTROLLER_INCLUDE_DIRS
     "${MODULE_ROOT_DIR}/examples/signaling_controller/" )

file(
  GLOB
  WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_SOURCE_FILES
  "${MODULE_ROOT_DIR}/examples/networking/*.c" )

set( WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_INCLUDE_DIRS
    "${MODULE_ROOT_DIR}/examples/networking/"
    "${MODULE_ROOT_DIR}/examples/networking/networkingLibwebsockets/" )

file(
  GLOB
  WEBRTC_APPLICATION_NETWORKING_UTILS_SOURCE_FILES
  "${MODULE_ROOT_DIR}/examples/networking/networking_utils/*.c" )

set( WEBRTC_APPLICATION_NETWORKING_UTILS_INCLUDE_DIRS
    "${MODULE_ROOT_DIR}/examples/networking/networking_utils" )

file(
  GLOB
  WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES
  "${MODULE_ROOT_DIR}/examples/base64/*.c"
  "${MODULE_ROOT_DIR}/examples/logging/*.c"
  "${MODULE_ROOT_DIR}/examples/message_queue/linux/*.c"
  "${MODULE_ROOT_DIR}/examples/timer_controller/*.c"
  "${MODULE_ROOT_DIR}/examples/string_utils/*.c"
  "${MODULE_ROOT_DIR}/examples/peer_connection/*.c"
  "${MODULE_ROOT_DIR}/examples/peer_connection/peer_connection_codec_helper/*.c"
  "${MODULE_ROOT_DIR}/examples/network_transport/*.c"
  "${MODULE_ROOT_DIR}/examples/network_transport/tcp_sockets_wrapper/ports/posix/*.c"
  "${MODULE_ROOT_DIR}/examples/network_transport/udp_sockets_wrapper/ports/posix/*.c"
  "${MODULE_ROOT_DIR}/examples/base64/*.c"
  "${MODULE_ROOT_DIR}/examples/base64/mbedtls/*.c" )

set( WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS
     "${MODULE_ROOT_DIR}/examples/base64/"
     "${MODULE_ROOT_DIR}/examples/logging/"
     "${MODULE_ROOT_DIR}/examples/message_queue/linux/"
     "${MODULE_ROOT_DIR}/examples/timer_controller/"
     "${MODULE_ROOT_DIR}/examples/string_utils"
     "${MODULE_ROOT_DIR}/examples/peer_connection/peer_connection_codec_helper"
     "${MODULE_ROOT_DIR}/examples/peer_connection/peer_connection_codec_helper/include"
     "${MODULE_ROOT_DIR}/examples/peer_connection"
     "${MODULE_ROOT_DIR}/examples/network_transport"
     "${MODULE_ROOT_DIR}/examples/network_transport/tcp_sockets_wrapper/include"
     "${MODULE_ROOT_DIR}/examples/network_transport/udp_sockets_wrapper/include"
     "${MODULE_ROOT_DIR}/examples/logging"
     "${MODULE_ROOT_DIR}/examples/base64"
     "configs/sigv4"
     "${MODULE_ROOT_DIR}/examples/demo_config" )

file( GLOB USRSCTP_SRC_FILES "${MODULE_ROOT_DIR}/examples/libusrsctp/*.c" )
list( APPEND WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES ${USRSCTP_SRC_FILES} )
list( APPEND WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS "${MODULE_ROOT_DIR}/examples/libusrsctp" )

file(GLOB METRIC_SRC_FILES "${MODULE_ROOT_DIR}/examples/metric/*.c")
list(APPEND WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES ${METRIC_SRC_FILES})
list( APPEND WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS "${MODULE_ROOT_DIR}/examples/metric" )

file(
  GLOB
  WEBRTC_APPLICATION_MASTER_MEDIA_SOURCE_FILES
  "${MODULE_ROOT_DIR}/examples/app_media_source/*.c" )

set( WEBRTC_APPLICATION_MASTER_MEDIA_INCLUDE_FILES
     "${MODULE_ROOT_DIR}/examples/app_media_source" )

file(
  GLOB
  WEBRTC_APPLICATION_SDP_CONTROLLER_SOURCE_FILES
  "${MODULE_ROOT_DIR}/examples/sdp_controller/*.c" )

set( WEBRTC_APPLICATION_SDP_CONTROLLER_INCLUDE_DIRS
     "${MODULE_ROOT_DIR}/examples/sdp_controller/" )

file(
  GLOB
  WEBRTC_APPLICATION_ICE_CONTROLLER_SOURCE_FILES
  "${MODULE_ROOT_DIR}/examples/ice_controller/*.c" )

set( WEBRTC_APPLICATION_ICE_CONTROLLER_INCLUDE_DIRS
    "${MODULE_ROOT_DIR}/examples/ice_controller/" )

# Include dependencies
## Include sigV4
include( ${MODULE_ROOT_DIR}/CMake/sigV4.cmake )

## MbedTLS
include( ${MODULE_ROOT_DIR}/CMake/mbedtls.cmake )

## Include libwebsockets
include( ${MODULE_ROOT_DIR}/CMake/libwebsockets.cmake )

## Include coreJSON
include( ${MODULE_ROOT_DIR}/CMake/coreJSON.cmake )

## Include signaling
include( ${MODULE_ROOT_DIR}/CMake/signaling.cmake )

## Include SDP
include( ${MODULE_ROOT_DIR}/CMake/sdp.cmake )

## Include STUN
include( ${MODULE_ROOT_DIR}/CMake/stun.cmake )

## Include ICE
include( ${MODULE_ROOT_DIR}/CMake/ice.cmake )

## RTCP
include( ${MODULE_ROOT_DIR}/CMake/rtcp.cmake )

## RTP
include( ${MODULE_ROOT_DIR}/CMake/rtp.cmake )

## LIBSRTP
include( ${MODULE_ROOT_DIR}/CMake/libsrtp.cmake )

## Include usrsctp
include( ${MODULE_ROOT_DIR}/CMake/libusrsctp.cmake )

## Include DCEP
include( ${MODULE_ROOT_DIR}/CMake/dcep.cmake )

file(
  GLOB
  WEBRTC_APPLICATION_MASTER_SOURCE_FILES
  "${MODULE_ROOT_DIR}/examples/master/*.c"
  "${MODULE_ROOT_DIR}/examples/app_common/*.c" )

set( WEBRTC_APPLICATION_MASTER_INCLUDE_DIRS
     "${MODULE_ROOT_DIR}/examples/master"
     "${MODULE_ROOT_DIR}/examples/app_common" )
