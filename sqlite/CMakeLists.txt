
add_definitions( -DSQLITE_HAS_CODEC)

set(SQLITE_SRC
    sqlite3.c
)

find_package(OpenSSL REQUIRED)

if( OpenSSL_FOUND )
    link_directories(${OPENSSL_LIBRARIES})
    include_directories(${OPENSSL_INCLUDE_DIR})
    message(STATUS "Using OpenSSL ${OPENSSL_VERSION}")
else()
    message(FATAL_ERROR "error, OpenSSL not found, please install: 'apt-get install libssl-dev' on Ubuntu")
endif()

add_library(sqlite STATIC ${SQLITE_SRC})
target_link_libraries(sqlite OpenSSL::Crypto)

if(UNIX)
    target_link_libraries(sqlite dl pthread)
endif()
