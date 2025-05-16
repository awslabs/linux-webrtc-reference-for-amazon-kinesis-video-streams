# Set the source directory explicitly
set(LIBUSRSCTP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/libraries/protocols/usrsctp)
set(LIBUSRSCTP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/libraries/protocols/usrsctp)

if(NOT EXISTS ${LIBUSRSCTP_SOURCE_DIR})
    message(FATAL_ERROR "The source directory ${LIBUSRSCTP_SOURCE_DIR} does not exist. Please ensure the path is correct.")
endif()

# Custom target to build the usrsctp library
add_custom_target(usrsctp-build ALL
    BYPRODUCTS ${LIBUSRSCTP_BUILD_DIR}/lib/libusrsctp.a
    COMMAND ${CMAKE_COMMAND} -S ${LIBUSRSCTP_SOURCE_DIR} -B ${LIBUSRSCTP_BUILD_DIR}
            -DCMAKE_INSTALL_PREFIX=${LIBUSRSCTP_BUILD_DIR}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -fPIC"
            -Dsctp_werror=0
            -Dsctp_build_programs=0
    COMMAND ${CMAKE_COMMAND} --build ${LIBUSRSCTP_BUILD_DIR} --target install
    COMMENT "Building libusrsctp library"
)

# Set the path to the built static library
set(LIBUSRSCTP_LIB ${LIBUSRSCTP_BUILD_DIR}/lib/libusrsctp.a)

# Ensure the static library is built before linking
add_library(usrsctp STATIC IMPORTED)
set_target_properties(usrsctp PROPERTIES
    IMPORTED_LOCATION ${LIBUSRSCTP_LIB}
)

target_include_directories( usrsctp INTERFACE
                            "${LIBUSRSCTP_SOURCE_DIR}/usrsctplib"
)

add_dependencies(usrsctp usrsctp-build)
