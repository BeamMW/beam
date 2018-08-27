set(TARGET_NAME beam-wallet)

add_definitions( -DSQLITE_HAS_CODEC )

set(WALLET_SRC
    wallet.h
    wallet.cpp
    negotiator.cpp
    wallet_network.cpp
    wallet_db.cpp
    keystore.cpp
)

add_library(wallet STATIC ${WALLET_SRC})
add_dependencies(wallet core p2p sqlite)
target_link_libraries(wallet core p2p sqlite)

add_executable(${TARGET_NAME} cli.cpp)

configure_file("${CMAKE_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_dependencies(${TARGET_NAME} wallet utility)
target_link_libraries(${TARGET_NAME} wallet utility)

add_subdirectory(unittests)
