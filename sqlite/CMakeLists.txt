
add_definitions( -DSQLITE_HAS_CODEC)

set(SQLITE_SRC
    sqlite3.c
)

add_library(sqlite STATIC ${SQLITE_SRC})
target_link_libraries(sqlite ${OPENSSL_LIBCRYPTO})

if(UNIX AND NOT ANDROID)
    target_link_libraries(sqlite dl pthread)
endif()
