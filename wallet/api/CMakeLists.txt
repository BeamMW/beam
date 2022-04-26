cmake_minimum_required(VERSION 3.13)

set(SOURCES
    base/api_base.cpp
    base/api_errors_imp.cpp
    i_wallet_api.cpp
    v6_0/v6_api.cpp
    v6_0/v6_api_handle.cpp
    v6_0/v6_api_parse.cpp
    v6_1/v6_1_api.cpp
    v6_1/v6_1_api_handle.cpp
    v6_1/v6_1_api_parse.cpp
    v6_1/v6_1_api_notify.cpp
    v6_3/v6_3_api.cpp
    v6_3/v6_3_api_handle.cpp
    v6_3/v6_3_api_parse.cpp
)

add_library(wallet_api STATIC
    ${SOURCES}
)

add_library(wallet_api_no_ipfs STATIC
    ${SOURCES}
)

if(BEAM_ATOMIC_SWAP_SUPPORT)
    set(SOURCES_SWAP
        v6_0/v6_api_swap_handle.cpp
        v6_0/v6_api_swap_parse.cpp
        cli/swap_client.cpp
        cli/swap_eth_client.cpp
    )

    target_sources(wallet_api
        PRIVATE ${SOURCES_SWAP}
    )

    target_sources(wallet_api_no_ipfs
        PRIVATE ${SOURCES_SWAP}
    )
    target_link_libraries(wallet_api
        PUBLIC 
            swap_offers_board
    )
endif()

target_link_libraries(wallet_api
    PUBLIC 
        wallet
)

target_link_libraries(wallet_api_no_ipfs
    PUBLIC
        wallet
        wallet_client_no_ipfs
        utility
)

if(BEAM_IPFS_SUPPORT)
    target_link_libraries(wallet_api PUBLIC wallet_ipfs)
endif()

if (NOT BEAM_WALLET_CLIENT_LIBRARY)
    set(WALLET_API_NAME wallet-api${BEAM_TARGET_SUFFIX})

    add_executable(${WALLET_API_NAME} cli/api_cli.cpp)
    
    if (BEAM_ATOMIC_SWAP_SUPPORT)
        target_sources(${WALLET_API_NAME} PRIVATE cli/api_cli_swap.cpp)
    endif()

    if (BEAM_ETH_API_EMULATION)
        target_compile_definitions(wallet_api PUBLIC BEAM_ETH_API_EMULATION)
    endif()

    target_link_libraries(${WALLET_API_NAME}
        PRIVATE
            wallet_api
            http
            cli
    )

    if(LINUX)
        target_link_libraries(${WALLET_API_NAME} 
            PRIVATE
                -static-libstdc++
                -static-libgcc)
    endif()

    if (BEAM_SIGN_PACKAGE AND WIN32)
        add_custom_command(
            TARGET ${WALLET_API_NAME} POST_BUILD
            COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} "${CMAKE_CURRENT_BINARY_DIR}\\${WALLET_API_NAME}.exe"
        )
    endif()

    if (BEAM_IPFS_SUPPORT AND WIN32)
        # TODO: This if hacky AF and beacuse we place everything in different folders.
        get_target_property(IPFS_DLL asio-ipfs IPFS_DLL)
        add_custom_command(
            TARGET ${WALLET_API_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${IPFS_DLL} ${CMAKE_CURRENT_BINARY_DIR}
        )
    endif()
endif()