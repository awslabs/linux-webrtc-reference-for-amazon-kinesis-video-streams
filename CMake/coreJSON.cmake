# This cmake file is used to include coreJSON as static library.
set(CMAKE_COREJSON_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/json/coreJSON)
include( ${CMAKE_COREJSON_DIRECTORY}/jsonFilePaths.cmake )

add_library( corejson )

target_sources( corejson
    PRIVATE
        ${JSON_SOURCES}
    PUBLIC
        ${JSON_INCLUDE_PUBLIC_DIRS}
)

target_include_directories( corejson PUBLIC
                            ${JSON_INCLUDE_PUBLIC_DIRS} )

