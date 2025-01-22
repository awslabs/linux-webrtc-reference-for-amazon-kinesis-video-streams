# This cmake file is used to include libwebsockets as static library.
set(CMAKE_SIGV4_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/crypto/SigV4-for-AWS-IoT-embedded-sdk)
include( ${CMAKE_SIGV4_DIRECTORY}/sigv4FilePaths.cmake )

add_library( sigv4
             ${SIGV4_SOURCES} )

target_include_directories( sigv4 PRIVATE
                            ${CMAKE_ROOT_DIRECTORY}/configs/sigv4
                            ${SIGV4_INCLUDE_PUBLIC_DIRS} )