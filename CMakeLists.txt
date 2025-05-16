cmake_minimum_required(VERSION 3.6.3)

# Set CMake policy CMP0048 to NEW to enable VERSION variables in project() command
# This addresses the following CMake warning:
# - Policy CMP0048 controls how project() command manages VERSION variables
# - Without this, PROJECT_VERSION* variables would be empty
# - Setting to NEW ensures proper version variable handlingcmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0048 NEW)

project(ProjectWebRTCLinuxApplication VERSION 0.0.1 LANGUAGES C)

set(CMAKE_ROOT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting CMAKE_BUILD_TYPE to Release by default")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

if( CMAKE_BUILD_TYPE MATCHES Debug )
  add_definitions( -DDEBUG=1 )
else()
  add_definitions( -DDEBUG=0 )
endif()

# Option to control linking with usrsctp
option(BUILD_USRSCTP_LIBRARY "Enable linking with usrsctp" ON)
option(ENABLE_ADDRESS_SANITIZER "Enable address sanitizer" OFF)
option(ENABLE_UNDEFINED_SANITIZER "Enable undefined sanitizer" OFF)
option(ENABLE_MEMORY_SANITIZER "Enable memory sanitizer" OFF)
option(ENABLE_THREAD_SANITIZER "Enable thread sanitizer" OFF)

# Option to enable metric logging
option(METRIC_PRINT_ENABLED "Enable Metric print logging" OFF)

# Option to enable media loopback
option(ENABLE_MEDIA_LOOPBACK "Enable media loopback" OFF)

if( ENABLE_ADDRESS_SANITIZER )
  set( CMAKE_C_FLAGS "-O0 -g -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls" )
elseif( ENABLE_UNDEFINED_SANITIZER )
  set( CMAKE_C_FLAGS "-O0 -g -fsanitize=undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls" )
elseif( ENABLE_MEMORY_SANITIZER )
  set( CMAKE_C_COMPILER /usr/bin/clang )
  set( CMAKE_CXX_COMPILER /usr/bin/clang++ )
  set( CMAKE_C_FLAGS "-O0 -g -fsanitize=memory -fno-omit-frame-pointer -fno-optimize-sibling-calls" )
elseif( ENABLE_THREAD_SANITIZER )
  set( CMAKE_C_COMPILER /usr/bin/clang )
  set( CMAKE_CXX_COMPILER /usr/bin/clang++ )
  set( CMAKE_C_FLAGS "-O0 -g -fsanitize=thread -fno-omit-frame-pointer -fno-optimize-sibling-calls" )
endif()

file(
  GLOB
  WEBRTC_APPLICATION_SIGNALING_CONTROLLER_SOURCE_FILES
  "examples/signaling_controller/*.c" )

set( WEBRTC_APPLICATION_SIGNALING_CONTROLLER_INCLUDE_DIRS
     "examples/signaling_controller/" )

file(
  GLOB
  WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_SOURCE_FILES
  "examples/networking/*.c" )

set( WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_INCLUDE_DIRS
    "examples/networking/"
    "examples/networking/networkingLibwebsockets/" )

file(
  GLOB
  WEBRTC_APPLICATION_NETWORKING_UTILS_SOURCE_FILES
  "examples/networking/networking_utils/*.c" )

set( WEBRTC_APPLICATION_NETWORKING_UTILS_INCLUDE_DIRS
    "examples/networking/networking_utils" )

file(
  GLOB
  WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES
  "examples/base64/*.c"
  "examples/logging/*.c"
  "examples/message_queue/linux/*.c"
  "examples/timer_controller/*.c"
  "examples/string_utils/*.c"
  "examples/peer_connection/*.c"
  "examples/peer_connection/peer_connection_codec_helper/*.c"
  "examples/network_transport/*.c"
  "examples/network_transport/tcp_sockets_wrapper/ports/posix/*.c"
  "examples/network_transport/udp_sockets_wrapper/ports/posix/*.c"
  "examples/base64/*.c"
  "examples/base64/mbedtls/*.c" )

set( WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS
     "examples/base64/"
     "examples/logging/"
     "examples/message_queue/linux/"
     "examples/timer_controller/"
     "examples/string_utils"
     "examples/peer_connection/peer_connection_codec_helper"
     "examples/peer_connection/peer_connection_codec_helper/include"
     "examples/peer_connection"
     "examples/network_transport"
     "examples/network_transport/tcp_sockets_wrapper/include"
     "examples/network_transport/udp_sockets_wrapper/include"
     "examples/logging"
     "examples/base64"
     "configs/sigv4"
     "examples/demo_config" )

if( BUILD_USRSCTP_LIBRARY )
    file( GLOB USRSCTP_SRC_FILES "examples/libusrsctp/*.c" )
    list( APPEND WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES ${USRSCTP_SRC_FILES} )
    list( APPEND WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS "examples/libusrsctp" )
endif()

if( METRIC_PRINT_ENABLED )
    file(GLOB METRIC_SRC_FILES "examples/metric/*.c")
    list(APPEND WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES ${METRIC_SRC_FILES})
    list( APPEND WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS "examples/metric" )
endif()

file(
  GLOB
  WEBRTC_APPLICATION_MASTER_MEDIA_SOURCE_FILES
  "examples/app_media_source/*.c" )

set( WEBRTC_APPLICATION_MASTER_MEDIA_INCLUDE_FILES
     "examples/app_media_source" )

file(
  GLOB
  WEBRTC_APPLICATION_SDP_CONTROLLER_SOURCE_FILES
  "examples/sdp_controller/*.c" )

set( WEBRTC_APPLICATION_SDP_CONTROLLER_INCLUDE_DIRS
     "examples/sdp_controller/" )

file(
  GLOB
  WEBRTC_APPLICATION_ICE_CONTROLLER_SOURCE_FILES
  "examples/ice_controller/*.c" )

set( WEBRTC_APPLICATION_ICE_CONTROLLER_INCLUDE_DIRS
    "examples/ice_controller/" )

# Include dependencies
## Include sigV4
include( ${CMAKE_ROOT_DIRECTORY}/CMake/sigV4.cmake )

## Include libwebsockets
include( ${CMAKE_ROOT_DIRECTORY}/CMake/libwebsockets.cmake )

## Include coreJSON
include( ${CMAKE_ROOT_DIRECTORY}/CMake/coreJSON.cmake )

## Include signaling
include( ${CMAKE_ROOT_DIRECTORY}/CMake/signaling.cmake )

## Include SDP
include( ${CMAKE_ROOT_DIRECTORY}/CMake/sdp.cmake )

## Include STUN
include( ${CMAKE_ROOT_DIRECTORY}/CMake/stun.cmake )

## Include ICE
include( ${CMAKE_ROOT_DIRECTORY}/CMake/ice.cmake )

## RTCP
include( ${CMAKE_ROOT_DIRECTORY}/CMake/rtcp.cmake )

## RTP
include( ${CMAKE_ROOT_DIRECTORY}/CMake/rtp.cmake )

## MbedTLS
include( ${CMAKE_ROOT_DIRECTORY}/CMake/mbedtls.cmake )

## LIBSRTP
include( ${CMAKE_ROOT_DIRECTORY}/CMake/libsrtp.cmake )

if(BUILD_USRSCTP_LIBRARY)
  ## Include usrsctp
  include( ${CMAKE_ROOT_DIRECTORY}/CMake/libusrsctp.cmake )

  ## Include DCEP
  include( ${CMAKE_ROOT_DIRECTORY}/CMake/dcep.cmake )
endif()

### Master Application
include( MasterExample.cmake )

### GStreamer Master Application
include( GstMasterExample.cmake )
