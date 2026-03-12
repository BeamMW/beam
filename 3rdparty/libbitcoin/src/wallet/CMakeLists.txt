set(TARGET_NAME libbitcoin_wallet)

file(GLOB SOURCES "*.cpp")
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/bitcoin_uri.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ek_private.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ek_public.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ek_token.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/encrypted_keys.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/message.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mini_keys.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/qrcode.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/stealth_address.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/stealth_receiver.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/stealth_sender.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/uri.cpp")
set(PARSE_ENCRYPTED_SOURCES "")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/wallet/*.hpp")

add_library(${TARGET_NAME} OBJECT ${SOURCES} ${PARSE_ENCRYPTED_SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4244")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4245")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4505")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4458")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4996")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4251")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4702")
elseif(APPLE OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-mismatched-tags)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-variable)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-missing-braces)
elseif(ANDROID)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-const-variable)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-tautological-constant-compare)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-variable)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-mismatched-tags)
else()
    target_compile_options(${TARGET_NAME} PUBLIC -fpermissive)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-but-set-variable)
endif()

target_link_libraries(${TARGET_NAME} Boost::boost)
