file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/chain/*.hpp")
add_library(libbitcoin_chain OBJECT	${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(libbitcoin_chain PRIVATE "/wd4245")
    target_compile_options(libbitcoin_chain PRIVATE "/wd4505")
    target_compile_options(libbitcoin_chain PRIVATE "/wd4706")
endif()