# This cmake file is used to include libwebsockets as static library.
set(CMAKE_STUN_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-stun)

# STUN library source files.
file(
  GLOB
  STUN_SOURCES
  "${CMAKE_STUN_DIRECTORY}/source/*.c" )

# STUN library public include directories.
set( STUN_INCLUDE_PUBLIC_DIRS
     "${CMAKE_STUN_DIRECTORY}/source/include" )

add_library( stun
             ${STUN_SOURCES} )

target_include_directories( stun PRIVATE
                            ${STUN_INCLUDE_PUBLIC_DIRS} )

target_link_libraries( stun PRIVATE )
