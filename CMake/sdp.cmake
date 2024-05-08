# This cmake file is used to include libwebsockets as static library.
set(CMAKE_SDP_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-sdp)
set(SDP_CONFIG_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/examples/sdp_controller)
include( ${CMAKE_SDP_DIRECTORY}/sdpFilePaths.cmake )

add_library( sdp
             ${SDP_SOURCES} )

target_include_directories( sdp PRIVATE
                            ${SDP_INCLUDE_PUBLIC_DIRS}
                            ${SDP_CONFIG_DIRECTORY} )
