cmake_minimum_required(VERSION 3.6.3)

project(ProjectWebRTCLinuxApplication VERSION 0.0.1 LANGUAGES C)

set(CMAKE_ROOT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting CMAKE_BUILD_TYPE to Release by default")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
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
  "examples/networking/networkingLibwebsockets/*.c" )

set( WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_INCLUDE_DIRS
    "examples/networking/"
    "examples/networking/networkingLibwebsockets/" )

file(
  GLOB
  WEBRTC_APPLICATION_MASTER_SOURCE_FILES
  "examples/master/*.c" )

set( WEBRTC_APPLICATION_MASTER_INCLUDE_DIRS
     "examples/master/" )

add_executable(
    WebRTCLinuxApplicationMaster
    ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_SOURCE_FILES}
    ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_SOURCE_FILES}
    ${WEBRTC_APPLICATION_MASTER_SOURCE_FILES} )

target_include_directories( WebRTCLinuxApplicationMaster PRIVATE
                            ${WEBRTC_APPLICATION_MASTER_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_INCLUDE_DIRS}
                            ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_INCLUDE_DIRS} )

# Include dependencies
## Include sigV4
include( ${CMAKE_ROOT_DIRECTORY}/CMake/sigV4.cmake )

## Set sigv4 include directories
message(STATUS "including sigv4 directories: ${SIGV4_INCLUDE_PUBLIC_DIRS}")
target_include_directories( WebRTCLinuxApplicationMaster PRIVATE
                            ${SIGV4_INCLUDE_PUBLIC_DIRS} )

## Include libwebsockets
include( ${CMAKE_ROOT_DIRECTORY}/CMake/libwebsockets.cmake )

## Set libwebsockets include directories
message(STATUS "including libwebsockets directories: ${LIBWEBSOCKETS_INCLUDE_DIRS}")
target_include_directories( WebRTCLinuxApplicationMaster PRIVATE
                            ${LIBWEBSOCKETS_INCLUDE_DIRS} )

## Include signaling
include( ${CMAKE_ROOT_DIRECTORY}/CMake/signaling.cmake )

## Set signaling include directories
message(STATUS "including signaling directories: ${SIGNALING_INCLUDE_PUBLIC_DIRS}")
target_include_directories( WebRTCLinuxApplicationMaster PRIVATE
                            ${SIGNALING_INCLUDE_PUBLIC_DIRS} )

# link application with dependencies
message(STATUS "linking websockets to WebRTCLinuxApplication")
target_link_libraries(WebRTCLinuxApplicationMaster websockets sigv4 signaling)
