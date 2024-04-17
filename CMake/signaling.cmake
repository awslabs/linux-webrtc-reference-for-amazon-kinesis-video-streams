# This cmake file is used to include libwebsockets as static library.
set(CMAKE_SIGNALING_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-signaling)
include( ${CMAKE_SIGNALING_DIRECTORY}/signalingFilePaths.cmake )

add_library( signaling
             ${SIGNALING_SOURCES} )

target_include_directories( signaling PRIVATE
                            ${SIGNALING_INCLUDE_PUBLIC_DIRS}
                            ${JSON_INCLUDE_PUBLIC_DIRS} )

target_link_libraries( signaling PRIVATE
                       corejson )

set( SIGNALING_INCLUDE_PUBLIC_DIRS ${SIGNALING_INCLUDE_PUBLIC_DIRS}
                                   ${JSON_INCLUDE_PUBLIC_DIRS} )

target_compile_definitions( signaling PUBLIC SIGNALING_DO_NOT_USE_CUSTOM_CONFIG )
