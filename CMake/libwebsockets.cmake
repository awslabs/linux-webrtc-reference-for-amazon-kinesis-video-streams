# This cmake file is used to include libwebsockets as static library.
set(CMAKE_LIBWEBSOCKETS_DIRECTORY ${CMAKE_ROOT_DIRECTORY}/libraries/networking/libwebsockets)

# Set options for libwebsockets
message(STATUS "setting options for libwebsockets")
set(LWS_WITH_MBEDTLS OFF CACHE INTERNAL "Disable libwebsockets mbedTLS")
set(LWS_WITH_HTTP2 ON CACHE INTERNAL "Compile with server support for HTTP/2")
set(LWS_HAVE_SSL_EXTRA_CHAIN_CERTS 1 CACHE INTERNAL "Have extra chain certs")
set(LWS_HAVE_OPENSSL_ECDH_H 1 CACHE INTERNAL "Enable ECDH")
set(LWS_WITHOUT_SERVER ON CACHE INTERNAL "Don't build the server part of the library")
set(LWS_WITHOUT_TESTAPPS ON CACHE INTERNAL "Don't build the libwebsocket-test-apps")
set(LWS_WITHOUT_TEST_SERVER_EXTPOLL ON CACHE INTERNAL "Don't build the test server version that uses external poll")
set(LWS_WITHOUT_TEST_PING ON CACHE INTERNAL "Don't build the ping test application")
set(LWS_WITHOUT_TEST_CLIENT ON CACHE INTERNAL "Don't build the client test application")
set(LWS_WITH_STATIC ON CACHE INTERNAL "Build the static version of the library")
set(LWS_WITH_SHARED OFF CACHE INTERNAL "Do not build the shared version of the library")
set(LWS_WITH_THREADPOOL OFF CACHE INTERNAL "Managed worker thread pool support (relies on pthreads)")
set(LWS_WITH_ZLIB OFF CACHE INTERNAL "Include zlib support (required for extensions)")
set(LWS_HAVE_PTHREAD_H ON CACHE INTERNAL "Have pthread")

# Include libwebsockets, then the library is named websockets
message(STATUS "adding libwebsockets subdirectory")
add_subdirectory(${CMAKE_LIBWEBSOCKETS_DIRECTORY})
