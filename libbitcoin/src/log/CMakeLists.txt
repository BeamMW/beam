file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/log/*.hpp")
add_library(libbitcoin_log OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(libbitcoin_log PRIVATE "/wd4459")
    target_compile_options(libbitcoin_log PRIVATE "/wd4996")
endif()