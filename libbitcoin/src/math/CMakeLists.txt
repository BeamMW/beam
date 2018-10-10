file(GLOB SOURCES "*.cpp")
file(GLOB EXTERNAL_SOURCES "external/*.c")
file(GLOB EXTERNAL_HEADERS "external/*.h")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/math/*.hpp")

add_library(libbitcoin_math OBJECT ${SOURCES} ${EXTERNAL_SOURCES} ${HEADERS} ${EXTERNAL_HEADERS})

target_include_directories(libbitcoin_math PRIVATE "${CMAKE_SOURCE_DIR}/secp256k1-zkp/include")

if(MSVC)
    target_compile_options(libbitcoin_math PRIVATE "/wd4701")
    target_compile_options(libbitcoin_math PRIVATE "/wd4245")
    target_compile_options(libbitcoin_math PRIVATE "/wd4505")
endif()