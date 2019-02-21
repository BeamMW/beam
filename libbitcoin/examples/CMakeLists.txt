set(TARGET_NAME sign_tx)

add_executable(${TARGET_NAME} sign_tx.cpp)

add_dependencies(${TARGET_NAME} libbitcoin secp256k1)
target_link_libraries(${TARGET_NAME} libbitcoin secp256k1)

target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/libbitcoin/include")

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4245")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4505")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4996")
elseif(APPLE)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-mismatched-tags)
else()
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-missing-braces)
endif()

set(TARGET_NAME get_info)

add_executable(${TARGET_NAME} get_info.cpp)
target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/libbitcoin/include")
add_dependencies(${TARGET_NAME} http libbitcoin)
target_link_libraries(${TARGET_NAME} http libbitcoin)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4245")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4505")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4996")
elseif(APPLE)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-mismatched-tags)
endif()

set(TARGET_NAME atomic_swap)

add_executable(${TARGET_NAME} atomic_swap.cpp)

target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/libbitcoin/include")
add_dependencies(${TARGET_NAME} http libbitcoin secp256k1)
target_link_libraries(${TARGET_NAME} http libbitcoin secp256k1)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4244")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4245")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4505")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4996")
elseif(APPLE)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-mismatched-tags)
else()
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-reorder)
endif()