add_definitions( -DSQLITE_HAS_CODEC )

add_test_snippet(wallet_test wallet node)
add_test_snippet(wallet_db_test wallet)
add_test_snippet(wallet_api_test wallet_api_proto)

if(BEAM_HW_WALLET)
    target_compile_definitions(wallet_test PRIVATE BEAM_HW_WALLET)
endif()

add_test_snippet(bitcoin_rpc_test wallet)
add_test_snippet(swap_test wallet node)

add_executable(offline offline.cpp)
add_dependencies(offline node wallet)
target_link_libraries(offline node wallet)
