cmake_minimum_required ( VERSION 3.22.0 )
project ( "Linux WebRTC App Coverity tests"
          VERSION 2.3.0
          LANGUAGES C )

# Allow the project to be organized into folders.
set_property( GLOBAL PROPERTY USE_FOLDERS ON )

# Use C90 if not specified.
if( NOT DEFINED CMAKE_C_STANDARD )
    set( CMAKE_C_STANDARD 99 )
endif()
if( NOT DEFINED CMAKE_C_STANDARD_REQUIRED )
    set( CMAKE_C_STANDARD_REQUIRED ON )
endif()

# If no configuration is defined, turn everything on.
if( NOT DEFINED COV_ANALYSIS )
    set( COV_ANALYSIS TRUE )
endif()

# Do not allow in-source build.
if( ${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR} )
    message( FATAL_ERROR "In-source build is not allowed. Please build in a separate directory, such as ${PROJECT_SOURCE_DIR}/build." )
endif()

# Set global path variables.
get_filename_component(__MODULE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MODULE_ROOT_DIR ${__MODULE_ROOT_DIR} CACHE INTERNAL "App repository root.")
set(CMAKE_ROOT_DIRECTORY ${MODULE_ROOT_DIR})

# Set output directories.
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib )

# ===================================== Coverity Analysis Configuration =================================================

if( COV_ANALYSIS )
    find_program(COV_BUILD cov-build)
    find_program(COV_ANALYZE cov-analyze)
    find_program(COV_CONFIG cov-configure)
    find_program(COV_FORMAT cov-format-errors)

    if( COV_BUILD AND COV_ANALYZE AND COV_FORMAT )
        # Include filepaths for source and include.
        include( ${MODULE_ROOT_DIR}/test/coverity/GetFilePath.cmake )

        # Target for Coverity analysis that builds the library.
        add_library( coverity_analysis
                     ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_NETWORKING_UTILS_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_MASTER_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_COMMON_UTILS_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_MASTER_MEDIA_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_SDP_CONTROLLER_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_ICE_CONTROLLER_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_MBEDTLS_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_COREHTTP_SOURCE_FILES}
                     ${WEBRTC_APPLICATION_LIBSRTP_SOURCE_FILES} )

        # public include path.
        target_include_directories( coverity_analysis PUBLIC
                                    ${WEBRTC_APPLICATION_MASTER_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_NETWORKING_LIBWEBSOCKETS_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_NETWORKING_UTILS_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_SIGNALING_CONTROLLER_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_COMMON_UTILS_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_MASTER_MEDIA_INCLUDE_FILES}
                                    ${WEBRTC_APPLICATION_SDP_CONTROLLER_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_ICE_CONTROLLER_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_MBEDTLS_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_COREHTTP_INCLUDE_DIRS}
                                    ${WEBRTC_APPLICATION_LIBSRTP_INCLUDE_DIRS}
                                    ${MODULE_ROOT_DIR}/test/coverity )

        # Remove inclusion of assert.
        target_compile_definitions( coverity_analysis PUBLIC DEBUG=0
                                                             NDEBUG=1
                                                             ENABLE_SCTP_DATA_CHANNEL=1
                                                             METRIC_PRINT_ENABLED=1 )

        # link application with dependencies, note that rt is librt providing message queue's APIs
        target_link_libraries( coverity_analysis
                               sigv4
                               signaling
                               corejson
                               sdp
                               ice
                               rtcp
                               rtp
                               stun
                               mbedtls
                               libsrtp
                               websockets
                               rt
                               pthread
                               usrsctp
                               dcep
        )

        # Directory for Coverity intermediate files.
        set(COVERITY_DIR "${CMAKE_BINARY_DIR}/coverity_report")
        message( STATUS "COVERITY_DIR: ${COVERITY_DIR}" )

        set( COVERAGE_SOURCE_DIR "${CMAKE_BINARY_DIR}" )
        message( STATUS "COVERAGE_SOURCE_DIR: ${COVERAGE_SOURCE_DIR}" )

        # Clean Coverity directory.
        add_custom_target( coverity_clean_dir
            COMMAND ${CMAKE_COMMAND} -E remove_directory ${COVERITY_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERITY_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_SOURCE_DIR}/covConfig
            COMMENT "Cleaning Coverity directory..."
        )

        # Clean build.
        add_custom_target( coverity_clean
            COMMAND ${CMAKE_COMMAND} --build ${COVERAGE_SOURCE_DIR} --target clean
            WORKING_DIRECTORY ${COVERAGE_SOURCE_DIR}
            COMMENT "Running Coverity clean..."
            DEPENDS coverity_clean_dir
        )

        # Configure.
        add_custom_target( coverity_configure
            COMMAND ${COV_CONFIG} --config covConfig/coverity.xml --compiler cc --comptype gcc --template
            WORKING_DIRECTORY ${COVERAGE_SOURCE_DIR}
            COMMENT "Running Coverity configure..."
            DEPENDS coverity_clean
        )

        # Individual Coverity targets.
        add_custom_target( coverity_build
            COMMAND ${COV_BUILD} --config covConfig/coverity.xml --emit-complementary-info --dir ${COVERAGE_SOURCE_DIR} ${CMAKE_COMMAND} --build ${COVERAGE_SOURCE_DIR} --target coverity_analysis -j ${PROCESSOR_COUNT}
            WORKING_DIRECTORY ${COVERAGE_SOURCE_DIR}
            COMMENT "Running Coverity build..."
            DEPENDS coverity_configure
        )

        add_custom_target( coverity_analyze
            COMMAND ${COV_ANALYZE}
                --dir ${COVERAGE_SOURCE_DIR}
                --tu-pattern \"file\(\'.*/examples/.*\'\)\"
            WORKING_DIRECTORY ${COVERAGE_SOURCE_DIR}
            DEPENDS coverity_build
        )

        add_custom_target(coverity_report
            COMMAND ${COV_FORMAT}
                --dir ${COVERAGE_SOURCE_DIR}
                --html-output ${COVERITY_DIR}
                --file "source"
            WORKING_DIRECTORY ${COVERAGE_SOURCE_DIR}
            COMMENT "Generating Coverity report..."
            DEPENDS coverity_analyze
        )

        # Meta-target that runs all Coverity steps.
        add_custom_target(coverity_scan DEPENDS coverity_report)

        message( STATUS "Coverity targets configured successfully" )
    else()
        message( WARNING "Coverity tools not found. Coverity targets will not be available." )
    endif()
endif()
