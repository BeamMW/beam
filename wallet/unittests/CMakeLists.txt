cmake_minimum_required(VERSION 3.13)

add_definitions( -DSQLITE_HAS_CODEC )

set(TEST_SHADERS test_shader.cpp)

add_library(wallet_test_node STATIC wallet_test_node.cpp)
target_link_libraries(wallet_test_node PUBLIC node wallet)


add_test_snippet(wallet_test wallet node mnemonic wallet wallet_client wallet_api wallet_test_node)
add_test_snippet(wallet_db_test wallet)
add_test_snippet(wallet_api_test wallet_api)
add_test_snippet(wallet_assets_test core node wallet pow assets wallet_test_node)
add_test_snippet(news_channels_test wallet_client node)
add_test_snippet(broadcasting_test wallet_client node)

if (BEAM_TEST_SHADERS)
    macro (add_shader shader )
        add_custom_command(
                       OUTPUT ${shader}.wasm
                       COMMAND clang -O0 
                                --target=wasm32 -I ${PROJECT_SOURCE_DIR}/bvm 
                                -std=c++17 
                                -fno-rtti 
                                -g
                                -Wl,--export-dynamic,--no-entry,--allow-undefined -nostdlib 
                                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.cpp --output ${shader}.wasm
                        COMMENT "Building shader ${shader}..."
                        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.cpp
                        VERBATIM
        )
        add_custom_target(
                        ${shader}_target 
                        ALL
                        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${shader}.wasm
                        )
    endmacro()

    add_shader(test_contract)
    add_shader(test_app)

    add_test_snippet(wallet_contract_test wallet_test_node dwarf)
    add_dependencies(wallet_contract_test
        test_contract_target test_app_target)
endif()

if(BEAM_HW_WALLET)
    target_compile_definitions(wallet_test PRIVATE BEAM_HW_WALLET)
endif()

if (BEAM_ATOMIC_SWAP_SUPPORT)
    add_test_snippet(bitcoin_rpc_test swap)
    add_test_snippet(swap_test node swap http wallet_test_node)
    add_test_snippet(swap_board_test wallet_client node)
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
    add_test_snippet(lelantus_test lelantus wallet node wallet_test_node)
endif()

#add_executable(offline offline.cpp)
#target_link_libraries(offline node wallet)
