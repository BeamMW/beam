set(TARGET_NAME beam-wallet)

add_definitions( -DSQLITE_HAS_CODEC )

set(WALLET_SRC
    wallet.h
    wallet.cpp
    wallet_transaction.cpp
    wallet_network.cpp
    wallet_db.cpp
    swap_transaction.cpp
    secstring2.cpp
    unittests/util.cpp
)

add_library(wallet STATIC ${WALLET_SRC})
add_dependencies(wallet core p2p sqlite)
target_link_libraries(wallet core p2p sqlite)

add_executable(${TARGET_NAME} cli.cpp)

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)

if(LINUX)
	target_link_libraries(${TARGET_NAME} -static-libstdc++ -static-libgcc)
endif()

configure_file("${CMAKE_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_dependencies(${TARGET_NAME} wallet utility)
target_link_libraries(${TARGET_NAME} wallet utility)

add_library(wallet_api_proto STATIC api.cpp)
add_dependencies(wallet_api_proto wallet utility)
target_link_libraries(wallet_api_proto wallet utility)

add_executable(wallet_api api_cli.cpp)
add_dependencies(wallet_api wallet utility wallet_api_proto)
target_link_libraries(wallet_api wallet utility wallet_api_proto)

add_subdirectory(unittests)
