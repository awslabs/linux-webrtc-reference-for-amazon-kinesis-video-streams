# This cmake file is used to include SDP as static library.
set(CMAKE_SDP_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-sdp)
set(SDP_CONFIG_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/examples/sdp_controller)
include( ${CMAKE_SDP_DIRECTORY}/sdpFilePaths.cmake )

add_library( sdp )

target_sources( sdp
    PRIVATE
        ${SDP_SOURCES}
    PUBLIC
        ${SDP_INCLUDE_PUBLIC_DIRS}
        ${SDP_CONFIG_DIRECTORY}
)

target_include_directories( sdp PUBLIC
                            ${SDP_INCLUDE_PUBLIC_DIRS}
                            ${SDP_CONFIG_DIRECTORY} )
