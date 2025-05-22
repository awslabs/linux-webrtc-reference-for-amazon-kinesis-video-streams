# This cmake file is used to include Signaling as static library.
set(CMAKE_SIGNALING_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-signaling)
include( ${CMAKE_SIGNALING_DIRECTORY}/signalingFilePaths.cmake )

add_library( signaling )

target_sources( signaling
    PRIVATE
        ${SIGNALING_SOURCES}
    PUBLIC
        ${SIGNALING_INCLUDE_PUBLIC_DIRS}
)

target_include_directories( signaling PUBLIC
                            ${SIGNALING_INCLUDE_PUBLIC_DIRS} )

target_link_libraries( signaling PRIVATE
                       corejson )

target_compile_definitions( signaling PUBLIC SIGNALING_DO_NOT_USE_CUSTOM_CONFIG )
