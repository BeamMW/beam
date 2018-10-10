file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/machine/*.hpp")
add_library(libbitcoin_machine OBJECT ${SOURCES} ${MACHINE})

if(MSVC)
    target_compile_options(libbitcoin_machine PRIVATE "/wd4245")
    target_compile_options(libbitcoin_machine PRIVATE "/wd4505")
endif()