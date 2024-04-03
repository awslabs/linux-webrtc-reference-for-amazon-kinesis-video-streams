# This cmake file is used to include libwebsockets as static library.
set(CMAKE_SIGNALING_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-signaling)
set(CMAKE_COREJSON_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-signaling/source/dependency/coreJSON)
include( ${CMAKE_SIGNALING_DIRECTORY}/signalingFilePaths.cmake )
include( ${CMAKE_COREJSON_DIRECTORY}/jsonFilePaths.cmake )

add_compile_definitions( SIGNALING_DO_NOT_USE_CUSTOM_CONFIG )

add_library( signaling
             ${SIGNALING_SOURCES}
             ${JSON_SOURCES} )

target_include_directories( signaling PRIVATE
                            ${SIGNALING_INCLUDE_PUBLIC_DIRS}
                            ${JSON_INCLUDE_PUBLIC_DIRS} )

set( SIGNALING_INCLUDE_PUBLIC_DIRS ${SIGNALING_INCLUDE_PUBLIC_DIRS}
                                   ${JSON_INCLUDE_PUBLIC_DIRS} )
