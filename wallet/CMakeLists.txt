cmake_minimum_required(VERSION 3.13)

add_subdirectory(core)
add_subdirectory(client)
add_subdirectory(api)

if(BEAM_ATOMIC_SWAP_SUPPORT)
    add_subdirectory(transactions/swaps)
endif()

if(BEAM_LELANTUS_SUPPORT)
    add_subdirectory(transactions/lelantus)
endif()

if(BEAM_CONFIDENTIAL_ASSETS_SUPPORT)
    add_subdirectory(transactions/assets)
    add_subdirectory(transactions/dex)
endif()

if (NOT ANDROID AND NOT IOS)
    if(BEAM_LASER_SUPPORT)
        add_subdirectory(laser)
    endif()
    if(NOT BEAM_WALLET_CLIENT_LIBRARY)
        add_subdirectory(cli)

        add_subdirectory(broadcaster)

        if(BEAM_TESTS_ENABLED)
            add_subdirectory(unittests)
        endif()
    endif()
endif()
