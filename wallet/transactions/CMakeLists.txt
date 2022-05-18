cmake_minimum_required(VERSION 3.13)

add_library(transactions INTERFACE)

if(BEAM_ATOMIC_SWAP_SUPPORT)
    add_subdirectory(swaps)
    target_link_libraries(transactions INTERFACE swap)
endif()

if(BEAM_LELANTUS_SUPPORT)
    add_subdirectory(lelantus)
    target_link_libraries(transactions INTERFACE lelantus)
endif()

if(BEAM_CONFIDENTIAL_ASSETS_SUPPORT)
    add_subdirectory(assets)
    add_subdirectory(dex)
    target_link_libraries(transactions 
        INTERFACE 
            assets
            dex
    )
endif()

