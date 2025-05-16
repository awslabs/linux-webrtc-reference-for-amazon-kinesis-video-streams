# This cmake file is used to include DCEP as static library.
set(CMAKE_DCEP_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/components/amazon-kinesis-video-streams-dcep)

# DCEP library source files.
file(
  GLOB
  DCEP_SOURCES
  "${CMAKE_DCEP_DIRECTORY}/source/*.c" )

# DCEP library public include directories.
set( DCEP_INCLUDE_PUBLIC_DIRS
     "${CMAKE_DCEP_DIRECTORY}/source/include" )

add_library( dcep )

target_sources( dcep
    PRIVATE
        ${DCEP_SOURCES}
    PUBLIC
        ${DCEP_INCLUDE_PUBLIC_DIRS}
)

target_include_directories( dcep PUBLIC
                            ${DCEP_INCLUDE_PUBLIC_DIRS} )
