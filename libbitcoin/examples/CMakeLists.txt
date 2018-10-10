set(TARGET_NAME sign_tx)

add_executable(${TARGET_NAME} sign_tx.cpp)

add_dependencies(${TARGET_NAME} libbitcoin secp256k1)
target_link_libraries(${TARGET_NAME} libbitcoin secp256k1)

target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/libbitcoin/include")

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4245")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4505")
endif()