cmake_minimum_required(VERSION 3.13)

add_subdirectory(core)
add_subdirectory(client)
add_subdirectory(api)
add_subdirectory(transactions)

add_library(wallet INTERFACE)
target_link_libraries(wallet 
    INTERFACE
        wallet_core
        transactions
)


if(BEAM_LASER_SUPPORT)
    add_subdirectory(laser)
endif()

if(BEAM_IPFS_SUPPORT)
    add_subdirectory(ipfs)
endif()

if(NOT BEAM_WALLET_CLIENT_LIBRARY)
    add_subdirectory(cli)
    add_subdirectory(broadcaster)
endif()

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittests)
endif()
