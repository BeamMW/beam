set(TARGET_NAME beam-wallet)

add_definitions( -DSQLITE_HAS_CODEC )

set(WALLET_SRC
    wallet.h
    wallet.cpp
    wallet_transaction.cpp
    wallet_network.cpp
    wallet_db.cpp
    swap_transaction.cpp
    keystore.cpp
    secstring2.cpp
    unittests/util.cpp
    bitcoin_rpc.h
    bitcoin_rpc.cpp
)

add_library(wallet STATIC ${WALLET_SRC})
add_dependencies(wallet core p2p sqlite libbitcoin)
target_link_libraries(wallet core p2p sqlite libbitcoin)
target_include_directories(wallet PRIVATE "${CMAKE_SOURCE_DIR}/libbitcoin/include")
if(MSVC)
    target_compile_options(wallet PRIVATE "/wd4245")
    target_compile_options(wallet PRIVATE "/wd4505")
endif()

add_executable(${TARGET_NAME} cli.cpp)

if(LINUX)
	target_link_libraries(${TARGET_NAME} -static-libstdc++ -static-libgcc)
endif()

configure_file("${CMAKE_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_dependencies(${TARGET_NAME} wallet utility)
target_link_libraries(${TARGET_NAME} wallet utility)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4245")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4505")
endif()

add_subdirectory(unittests)
