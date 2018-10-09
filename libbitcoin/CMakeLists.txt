#cmake_minimum_required(VERSION 3.5)

#project(libbitcoin LANGUAGES C CXX)
set(TARGET_NAME libbitcoin)

set(LIBBITCOIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/bitcoin/bitcoin)

add_subdirectory(src)

add_library(${TARGET_NAME} STATIC
    src/error.cpp
    ${CHAIN_SOURCES}
    ${CHAIN_HEADERS}
    ${CONFIG_SOURCES}
    ${CONFIG_HEADERS}
    ${FORMATS_SOURCES}
    ${FORMATS_HEADERS}
    ${LOG_SOURCES}
    ${LOG_HEADERS}
    ${MACHINE_SOURCES}
    ${MACHINE_HEADERS}
    ${MATH_SOURCES}
    ${MATH_HEADERS}
    ${EXTERNAL_MATH_SOURCES}
    ${EXTERNAL_MATH_HEADERS}
    ${MESSAGE_SOURCES}
    ${MESSAGE_HEADERS}
    ${UNICODE_SOURCES}
    ${UNICODE_HEADERS}
    ${UTILITY_SOURCES}
    ${UTILITY_HEADERS}
    ${WALLET_HEADERS}
    ${WALLET_SOURCES}
    ${PARSE_ENCRYPTED_SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)