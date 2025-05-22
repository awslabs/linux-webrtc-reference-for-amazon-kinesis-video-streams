# mbedtls.cmake
set(MBEDTLS_SOURCE_DIR "${CMAKE_ROOT_DIRECTORY}/libraries/crypto/mbedtls")
set(MBEDTLS_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/configs/mbedtls")
set(MBEDTLS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/libraries/crypto/mbedtls")
set(MBEDTLS_BUILD_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/libraries/crypto/mbedtls/include")

# Create directories
file(MAKE_DIRECTORY ${MBEDTLS_BUILD_DIR})
file(MAKE_DIRECTORY ${MBEDTLS_BUILD_INCLUDE_DIR})

# Copy headers
file(COPY ${MBEDTLS_SOURCE_DIR}/include/ DESTINATION ${MBEDTLS_BUILD_INCLUDE_DIR})
file(COPY ${MBEDTLS_CONFIG_DIR}/mbedtls_custom_config.h DESTINATION ${MBEDTLS_BUILD_INCLUDE_DIR})

# Collect source files more specifically
file(GLOB MBEDTLS_SOURCES "${MBEDTLS_SOURCE_DIR}/library/ssl_*.c")
file(GLOB MBEDX509_SOURCES "${MBEDTLS_SOURCE_DIR}/library/x509*.c")
file(GLOB MBEDCRYPTO_SOURCES "${MBEDTLS_SOURCE_DIR}/library/*.c")

# Clean up crypto sources
list(FILTER MBEDCRYPTO_SOURCES EXCLUDE REGEX "(ssl|x509)_.*.c$")

# Create libraries
add_library(mbedtls ${MBEDTLS_SOURCES})
add_library(mbedx509 ${MBEDX509_SOURCES})
add_library(mbedcrypto ${MBEDCRYPTO_SOURCES})

# Set output directories for the libraries
set_target_properties(mbedcrypto mbedx509 mbedtls
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${MBEDTLS_BUILD_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${MBEDTLS_BUILD_DIR}"
    POSITION_INDEPENDENT_CODE ON
)

# Set up modern CMake target properties
foreach(target IN ITEMS mbedcrypto mbedx509 mbedtls)
    target_include_directories(${target}
        PUBLIC
            ${MBEDTLS_BUILD_INCLUDE_DIR}
    )

    target_compile_definitions(${target}
        PUBLIC
            MBEDTLS_CONFIG_FILE="mbedtls_custom_config.h"
    )
endforeach()

# Set up dependencies
target_link_libraries(mbedx509 PUBLIC mbedcrypto)
target_link_libraries(mbedtls PUBLIC mbedx509)

# Suppress specific warning for ssl_tls.c
set_source_files_properties(
    "${MBEDTLS_SOURCE_DIR}/library/ssl_tls.c"
    PROPERTIES COMPILE_FLAGS -Wno-stringop-overflow
)
