# This cmake file is used to include libwebsockets as static library.
set(CMAKE_SIGNALING_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-sdp)
include( ${CMAKE_SIGNALING_DIRECTORY}/sdpFilePaths.cmake )

add_library( sdp
             ${SDP_SOURCES} )

target_include_directories( sdp PRIVATE
                            ${SDP_INCLUDE_PUBLIC_DIRS} )

target_compile_definitions( sdp PUBLIC SDP_DO_NOT_USE_CUSTOM_CONFIG )
