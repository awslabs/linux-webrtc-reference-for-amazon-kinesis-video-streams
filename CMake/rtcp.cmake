# This cmake file is used to include libwebsockets as static library.
set(CMAKE_RTCP_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-rtcp)
include(${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-rtcp/rtcpFilePaths.cmake)

add_library( rtcp )

target_sources( rtcp
    PRIVATE
        ${RTCP_SOURCES}
    PUBLIC
        ${RTCP_INCLUDE_PUBLIC_DIRS}
)

target_include_directories( rtcp PUBLIC
                            ${RTCP_INCLUDE_PUBLIC_DIRS} )
