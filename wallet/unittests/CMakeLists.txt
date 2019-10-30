cmake_minimum_required(VERSION 3.11)

add_definitions( -DSQLITE_HAS_CODEC )

add_test_snippet(wallet_test wallet node mnemonic)
add_test_snippet(wallet_db_test wallet)
add_test_snippet(wallet_api_test wallet_api_proto)

if(BEAM_HW_WALLET)
    target_compile_definitions(wallet_test PRIVATE BEAM_HW_WALLET)
endif()

if (BEAM_ATOMIC_SWAP_SUPPORT)
    add_test_snippet(bitcoin_rpc_test swap)
    add_test_snippet(swap_test node swap http)
    add_test_snippet(swap_board_test wallet node)
    add_test_snippet(electrum_test swap)
endif()


add_executable(offline offline.cpp)
target_link_libraries(offline node wallet)
