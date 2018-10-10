file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/unicode/*.hpp")

add_library(libbitcoin_unicode OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(libbitcoin_unicode PRIVATE "/wd4389")
endif()