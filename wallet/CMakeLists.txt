cmake_minimum_required(VERSION 3.13)

add_subdirectory(core)
add_subdirectory(client)


if(BEAM_ATOMIC_SWAP_SUPPORT)
    add_subdirectory(transactions/swaps)
endif()

if (NOT ANDROID AND NOT IOS)

    if(BEAM_CONFIDENTIAL_ASSETS_SUPPORT)
        add_subdirectory(transactions/assets)
    endif()

    if(BEAM_LASER_SUPPORT)
        add_subdirectory(laser)
    endif()

    add_subdirectory(cli)

    add_subdirectory(api)

    add_subdirectory(broadcaster)

    add_subdirectory(unittests)

    add_subdirectory(service)

endif()


