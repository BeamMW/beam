file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/message/*.hpp")

add_library(libbitcoin_message OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(libbitcoin_message PRIVATE "/wd4245")
    target_compile_options(libbitcoin_message PRIVATE "/wd4505")
    target_compile_options(libbitcoin_message PRIVATE "/wd4996")
else()
    target_compile_options(libbitcoin_message PRIVATE -Wno-mismatched-tags)
endif()