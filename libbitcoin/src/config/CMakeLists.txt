file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/config/*.hpp")

add_library(libbitcoin_config OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(libbitcoin_config PRIVATE "/wd4996")
endif()