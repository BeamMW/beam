cmake_minimum_required(VERSION 3.11)

set(SQLITE_SRC
    sqlite3.c
)

add_library(sqlite STATIC ${SQLITE_SRC})

target_compile_definitions(sqlite PUBLIC SQLITE_HAS_CODEC)

target_link_libraries(sqlite PRIVATE OpenSSL::Crypto)
if(OPENSSL_USE_STATIC_LIBS AND MSVC)
    target_link_libraries(sqlite PRIVATE crypt32.lib)
    if(MSVC_VERSION GREATER_EQUAL 1900)
        target_link_libraries(sqlite PRIVATE legacy_stdio_definitions.lib)
    endif()
endif()

if(UNIX AND NOT ANDROID)
    target_link_libraries(sqlite PRIVATE ${CMAKE_DL_LIBS} pthread)
endif()

if(ANDROID)
    # On Android there's no tmp partition, so always use a MEMORY temp store.
    target_compile_definitions(sqlite PRIVATE SQLITE_TEMP_STORE=3)
endif()
