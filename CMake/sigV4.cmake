# This cmake file is used to include libwebsockets as static library.
set(CMAKE_SIGV4_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/crypto/SigV4-for-AWS-IoT-embedded-sdk)
include( ${CMAKE_SIGV4_DIRECTORY}/sigv4FilePaths.cmake )

add_compile_definitions( SIGV4_DO_NOT_USE_CUSTOM_CONFIG )

add_library( sigv4
             ${SIGV4_SOURCES} )

target_include_directories( sigv4 PRIVATE
                            ${SIGV4_INCLUDE_PUBLIC_DIRS} )