# Set the source directory explicitly
set(LIBSRTP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/libraries/protocols/libsrtp)
set(LIBSRTP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/libraries/protocols/libsrtp)

if(NOT EXISTS ${LIBSRTP_SOURCE_DIR})
    message(FATAL_ERROR "The source directory ${LIBSRTP_SOURCE_DIR} does not exist. Please ensure the path is correct.")
endif()

file(
  GLOB
  LIBSRTP_GLOB_SOURCE_FILES
  "${LIBSRTP_SOURCE_DIR}/srtp/*.c"
  "${LIBSRTP_SOURCE_DIR}/crypto/kernel/*.c"
  "${LIBSRTP_SOURCE_DIR}/crypto/math/*.c"
  "${LIBSRTP_SOURCE_DIR}/crypto/replay/*.c" )

set( LIBSRTP_SOURCE_FILES
     ${LIBSRTP_GLOB_SOURCE_FILES}
     "${LIBSRTP_SOURCE_DIR}/crypto/cipher/aes.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/cipher/aes_gcm_mbedtls.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/cipher/aes_icm_mbedtls.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/cipher/cipher.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/cipher/cipher_test_cases.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/cipher/null_cipher.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/hash/auth_test_cases.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/hash/auth.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/hash/hmac_mbedtls.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/hash/null_auth.c"
     "${LIBSRTP_SOURCE_DIR}/crypto/hash/sha1.c" )

set( LIBSRTP_INCLUDE_DIRS
     "${LIBSRTP_SOURCE_DIR}/include"
     "${LIBSRTP_SOURCE_DIR}/crypto/include"
     "${CMAKE_ROOT_DIRECTORY}/examples/libsrtp" )

add_library( libsrtp )

target_sources( libsrtp
    PRIVATE
        ${LIBSRTP_SOURCE_FILES}
    PUBLIC
        ${LIBSRTP_INCLUDE_DIRS}
)

target_include_directories( libsrtp PUBLIC
                            ${LIBSRTP_INCLUDE_DIRS} )

target_link_libraries( libsrtp PRIVATE
                       mbedtls )
