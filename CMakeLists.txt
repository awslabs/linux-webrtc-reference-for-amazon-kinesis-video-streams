cmake_minimum_required(VERSION 3.6.3)

project(ProjectWebRTCLinuxApplication VERSION 0.0.1 LANGUAGES C)

set(CMAKE_ROOT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting CMAKE_BUILD_TYPE to Release by default")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

file(
  GLOB
  WEBRTC_APPLICATION_MASTER_SOURCE_FILES
  "examples/master/*.c" )

add_executable(
    WebRTCLinuxApplicationMaster
    ${WEBRTC_APPLICATION_MASTER_SOURCE_FILES} )

# Include dependencies
include( ${CMAKE_ROOT_DIRECTORY}/CMake/libwebsockets.cmake )

# Set the include directories
message(STATUS "including libwebsockets directories: ${LIBWEBSOCKETS_INCLUDE_DIRS}")
target_include_directories(WebRTCLinuxApplicationMaster PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})

# add_dependencies(WebRTCLinuxApplication libwebsockets)
message(STATUS "linking websockets to WebRTCLinuxApplication")
target_link_libraries(WebRTCLinuxApplicationMaster websockets)
