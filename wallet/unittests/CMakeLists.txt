cmake_minimum_required(VERSION 3.13)

add_definitions( -DSQLITE_HAS_CODEC )

set(TEST_SHADERS test_shader.cpp)

add_library(wallet_test_node STATIC wallet_test_node.cpp)
target_link_libraries(wallet_test_node PUBLIC node wallet)

configure_file("../../bvm/Shaders/vault/contract.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault/contract.wasm" COPYONLY)
configure_file("../../bvm/Shaders/vault/app.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault/app.wasm" COPYONLY)
configure_file("../../bvm/Shaders/upgradable2/contract.wasm" "${CMAKE_CURRENT_BINARY_DIR}/upgradable2/contract.wasm" COPYONLY)
configure_file("../../bvm/Shaders/upgradable2/Test/test_v0.wasm" "${CMAKE_CURRENT_BINARY_DIR}/upgradable2/Test/test_v0.wasm" COPYONLY)
configure_file("../../bvm/Shaders/upgradable2/Test/test_v1.wasm" "${CMAKE_CURRENT_BINARY_DIR}/upgradable2/Test/test_v1.wasm" COPYONLY)
configure_file("../../bvm/Shaders/upgradable2/Test/test_app.wasm" "${CMAKE_CURRENT_BINARY_DIR}/upgradable2/Test/test_app.wasm" COPYONLY)


add_test_snippet(wallet_test wallet node mnemonic wallet_client wallet_api wallet_test_node ethash libbitcoin bvm)
add_test_snippet(wallet_db_test wallet_core)
add_test_snippet(wallet_api_test wallet_api)
add_test_snippet(wallet_assets_test core node wallet_core pow assets wallet_test_node)
add_test_snippet(news_channels_test wallet_client node)
add_test_snippet(broadcasting_test broadcast_gateway node)

if (BEAM_TEST_SHADERS)
    add_test_snippet(wallet_contract_test wallet_test_node)

    function (add_shader shader )
        add_custom_command(TARGET wallet_contract_test 
                       PRE_BUILD 
                       COMMAND clang -O3 
                                --target=wasm32 -I ${PROJECT_SOURCE_DIR}/bvm 
                                -std=c++17 
                                -fno-rtti 
                                -Wl,--export-dynamic,--no-entry,--allow-undefined -nostdlib 
                                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.cpp --output ${shader}.wasm
                        COMMENT "Building shader ${shader}..."
                        VERBATIM
    )
    endfunction()

    add_shader(test_contract)
    add_shader(test_app)
endif()

if(BEAM_HW_WALLET)
    target_compile_definitions(wallet_test PRIVATE BEAM_HW_WALLET)
endif()

if (BEAM_ATOMIC_SWAP_SUPPORT)
    add_test_snippet(bitcoin_rpc_test swap)
    add_test_snippet(swap_test node swap http wallet_test_node)
    add_test_snippet(swap_board_test swap_offers_board node)
    add_test_snippet(electrum_test swap)
    #add_test_snippet(ethereum_test swap)
endif()

if (BEAM_LASER_SUPPORT)
    add_test_snippet(laser_test_open node laser wallet_test_node)
    add_test_snippet(laser_test_open_fail node laser wallet_test_node)
    add_test_snippet(laser_test_close node laser wallet_test_node)
    add_test_snippet(laser_test_drop node laser wallet_test_node)
    add_test_snippet(laser_test_send node laser wallet_test_node)
    add_test_snippet(laser_test_send_fail node laser wallet_test_node)
    add_test_snippet(laser_test_listen_first node laser wallet_test_node)
    add_test_snippet(laser_test_listen_second node laser wallet_test_node)
    add_test_snippet(laser_test_auto_close node laser wallet_test_node)
    add_test_snippet(laser_test_delete node laser wallet_test_node)
endif()

if (BEAM_LELANTUS_SUPPORT)
    add_test_snippet(lelantus_test lelantus wallet_core node wallet_test_node)
endif()

if (BEAM_IPFS_SUPPORT AND WIN32)
    # TODO: This if hacky AF and beacuse we place everything in different folders.
    get_target_property(IPFS_DLL asio-ipfs IPFS_DLL)
    add_custom_command(
        TARGET wallet_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${IPFS_DLL} ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

#add_executable(offline offline.cpp)
#target_link_libraries(offline node wallet_core)
