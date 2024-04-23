# This cmake file is used to include libwebsockets as static library.
set(CMAKE_ICE_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/iceaws)

# ICE library source files.
file(
  GLOB
  ICE_SOURCES
  "${CMAKE_ICE_DIRECTORY}/*.c" )

# ICE library public include directories.
set( ICE_INCLUDE_PUBLIC_DIRS
     "${CMAKE_ICE_DIRECTORY}/" )

add_library( ice
             ${ICE_SOURCES} )

target_include_directories( ice PRIVATE
                            ${ICE_INCLUDE_PUBLIC_DIRS} )
