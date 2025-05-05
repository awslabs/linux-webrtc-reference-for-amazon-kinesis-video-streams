cmake_minimum_required(VERSION 3.6.3)

project(ProjectWebRTCLinuxApplication VERSION 0.0.1 LANGUAGES C)

set(CMAKE_ROOT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting CMAKE_BUILD_TYPE to Release by default")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Option to control linking with usrsctp
option(BUILD_USRSCTP_LIBRARY "Enable linking with usrsctp" ON)
option(ENABLE_ADDRESS_SANITIZER "Enable address sanitizer" OFF)
option(ENABLE_UNDEFINED_SANITIZER "Enable undefined sanitizer" OFF)
option(ENABLE_MEMORY_SANITIZER "Enable memory sanitizer" OFF)
option(ENABLE_THREAD_SANITIZER "Enable thread sanitizer" OFF)

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

include(${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-rtcp/rtcpFilePaths.cmake)
include(${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-rtp/rtpFilePaths.cmake)
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
  if(BUILD_USRSCTP_LIBRARY)
    "examples/libusrsctp/*.c"
  endif()
  "examples/peer_connection/*.c"
  "examples/peer_connection/peer_connection_codec_helper/*.c"
  "examples/network_transport/*.c"
  "examples/network_transport/tcp_sockets_wrapper/ports/posix/*.c"
  "examples/network_transport/udp_sockets_wrapper/ports/posix/*.c"
  "examples/metric/*.c"
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
     "examples/metric"
     "examples/libsrtp"
     if(BUILD_USRSCTP_LIBRARY)
       "examples/libusrsctp"
     endif()
     "examples/logging"
     "examples/base64"
     "configs/mbedtls"
     "configs/sigv4"
     "examples/demo_config" )

file(
  GLOB
  WEBRTC_APPLICATION_MASTER_MEDIA_SOURCE_FILES
  "examples/app_media_source/*.c" )

set( WEBRTC_APPLICATION_MASTER_MEDIA_INCLUDE_FILES
     "examples/app_media_source" )

file(
  GLOB
  WEBRTC_APPLICATION_MBEDTLS_SOURCE_FILES
  "libraries/crypto/mbedtls/library/*.c" )

set( WEBRTC_APPLICATION_MBEDTLS_INCLUDE_DIRS
     "libraries/crypto/mbedtls/include"
     "configs/mbedtls" )

set_source_files_properties("libraries/crypto/mbedtls/library/ssl_tls.c" PROPERTIES COMPILE_FLAGS -Wno-stringop-overflow)

file(
  GLOB
  WEBRTC_APPLICATION_COREHTTP_SOURCE_FILES
  "libraries/protocols/coreHTTP/source/*.c"
  "libraries/protocols/coreHTTP/source/dependency/3rdparty/llhttp/src/*.c" )

set( WEBRTC_APPLICATION_COREHTTP_INCLUDE_DIRS
     "libraries/protocols/coreHTTP/source/include"
     "libraries/protocols/coreHTTP/source/interface"
     "libraries/protocols/coreHTTP/source/dependency/3rdparty/llhttp/include" )

file(
  GLOB
  WEBRTC_APPLICATION_LIBSRTP_GLOB_SOURCE_FILES
  "libraries/protocols/libsrtp/srtp/*.c"
  "libraries/protocols/libsrtp/crypto/kernel/*.c"
  "libraries/protocols/libsrtp/crypto/math/*.c"
  "libraries/protocols/libsrtp/crypto/replay/*.c" )

set( WEBRTC_APPLICATION_LIBSRTP_SOURCE_FILES
     ${WEBRTC_APPLICATION_LIBSRTP_GLOB_SOURCE_FILES}
     "libraries/protocols/libsrtp/crypto/cipher/aes.c"
     "libraries/protocols/libsrtp/crypto/cipher/aes_gcm_mbedtls.c"
     "libraries/protocols/libsrtp/crypto/cipher/aes_icm_mbedtls.c"
     "libraries/protocols/libsrtp/crypto/cipher/cipher.c"
     "libraries/protocols/libsrtp/crypto/cipher/cipher_test_cases.c"
     "libraries/protocols/libsrtp/crypto/cipher/null_cipher.c"
     "libraries/protocols/libsrtp/crypto/hash/auth_test_cases.c"
     "libraries/protocols/libsrtp/crypto/hash/auth.c"
     "libraries/protocols/libsrtp/crypto/hash/hmac_mbedtls.c"
     "libraries/protocols/libsrtp/crypto/hash/null_auth.c"
     "libraries/protocols/libsrtp/crypto/hash/sha1.c" )

set( WEBRTC_APPLICATION_LIBSRTP_INCLUDE_DIRS
     "libraries/protocols/libsrtp/include"
     "libraries/protocols/libsrtp/crypto/include" )

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

if(BUILD_USRSCTP_LIBRARY)
  ## Include usrsctp
  include( ${CMAKE_ROOT_DIRECTORY}/CMake/libusrsctp.cmake )

  ## Include DCEP
  include( ${CMAKE_ROOT_DIRECTORY}/CMake/dcep.cmake )
endif()

# usrsctp library public include directories.
set( SCTP_INCLUDE_PUBLIC_DIRS
     "${CMAKE_ROOT_DIRECTORY}/libraries/protocols/usrsctp/usrsctplib" )

### Master Application
include( MasterExample.cmake )

### GStreamer Master Application
include( GstMasterExample.cmake )
