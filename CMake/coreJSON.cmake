# This cmake file is used to include libwebsockets as static library.
set(CMAKE_COREJSON_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/json/coreJSON)
include( ${CMAKE_COREJSON_DIRECTORY}/jsonFilePaths.cmake )

add_library( corejson
             ${JSON_SOURCES} )

target_include_directories( corejson PRIVATE
                            ${JSON_INCLUDE_PUBLIC_DIRS} )

