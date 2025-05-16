# mbedtls.cmake
set(MBEDTLS_LIB_DIR ${CMAKE_ROOT_DIRECTORY}/libraries/crypto/mbedtls)

file(
  GLOB
  MBEDTLS_SOURCE_FILES
  "${MBEDTLS_LIB_DIR}/library/*.c" )

set( MBEDTLS_INCLUDE_DIRS
     "${MBEDTLS_LIB_DIR}/include"
     "${CMAKE_ROOT_DIRECTORY}/configs/mbedtls" )

set_source_files_properties( "${MBEDTLS_LIB_DIR}/library/ssl_tls.c" PROPERTIES COMPILE_FLAGS -Wno-stringop-overflow )

add_library( mbedtls )

target_sources( mbedtls
    PRIVATE
        ${MBEDTLS_SOURCE_FILES}
    PUBLIC
        ${MBEDTLS_INCLUDE_DIRS}
)

target_compile_definitions( mbedtls PUBLIC
                            MBEDTLS_CONFIG_FILE="mbedtls_custom_config.h" )

target_include_directories( mbedtls PUBLIC
                            ${MBEDTLS_INCLUDE_DIRS} )
