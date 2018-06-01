
add_definitions( -DSQLITE_HAS_CODEC)

set(SQLITE_SRC
    sqlite3.c
)

find_package(OpenSSL REQUIRED)

if( OpenSSL_FOUND )
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    link_directories(${OPENSSL_LIBRARIES})
    include_directories(${OPENSSL_INCLUDE_DIR})
    message(STATUS "Using OpenSSL ${OPENSSL_VERSION}")
else()
    message(FATAL_ERROR "error, OpenSSL not found, please install: 'apt-get install libssl-dev' on Ubuntu")
endif()

# cmake can't find static lib on macos, harcoded
if(APPLE)
    set(STATIC_OPENSSL /usr/local/opt/openssl/lib/libcrypto.a)
elseif(UNIX)
    set(STATIC_OPENSSL /usr/lib/x86_64-linux-gnu/libcrypto.a)
else()
    set(STATIC_OPENSSL OpenSSL::Crypto)
endif()

add_library(sqlite STATIC ${SQLITE_SRC})

target_link_libraries(sqlite ${STATIC_OPENSSL})

if(UNIX)
    target_link_libraries(sqlite dl pthread)
endif()
