file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/utility/*.hpp")

add_library(libbitcoin_utility OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(libbitcoin_utility PRIVATE "/wd4244")
    target_compile_options(libbitcoin_utility PRIVATE "/wd4996")
endif()