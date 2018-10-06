set(TEST_TOOLS_SRC
	tools/base_node_connection.h
	tools/base_node_connection.cpp
	tools/coins_checker.h
	tools/coins_checker.cpp
	tools/new_tx_tests.h
	tools/new_tx_tests.cpp
	tools/tx_generator.h
	tools/tx_generator.cpp
)

add_library(test_tools STATIC ${TEST_TOOLS_SRC})
add_dependencies(test_tools node)

function(add_functional_test_snippet EXE_NAME LIB_NAME)
    add_executable(${EXE_NAME} ${EXE_NAME}.cpp)
    add_dependencies(${EXE_NAME} test_tools ${LIB_NAME} utility)
    target_link_libraries(${EXE_NAME} test_tools ${LIB_NAME} utility)
endfunction()

add_functional_test_snippet(send_to_node_10tx_test node)
add_functional_test_snippet(send_empty_kernels_test node)
add_functional_test_snippet(send_tx_with_unsorted_test node)
add_functional_test_snippet(send_big_tx_test node)
add_functional_test_snippet(send_wrong_tx_test node)
add_functional_test_snippet(send_wrong_newtip_test node)
add_functional_test_snippet(send_data_missing_test node)
add_functional_test_snippet(send_peer_info_test node)
add_functional_test_snippet(spam_transactions_test node)
add_functional_test_snippet(send_gethdr_test node)
add_functional_test_snippet(send_getmined_test node)
add_functional_test_snippet(check_mined_test node)
add_functional_test_snippet(send_broken_new_tx_test node)
add_functional_test_snippet(send_getproofutxo_test node)
add_functional_test_snippet(spend_mined_coins_test node)
add_functional_test_snippet(spam_havetransactions_test node)
add_functional_test_snippet(fraction_coin_test node)
add_functional_test_snippet(send_tx_with_equal_commitment_test node)
add_functional_test_snippet(send_tx_with_zero_output_test node)
add_functional_test_snippet(spend_fee_test node)
add_functional_test_snippet(try_spend_unknown_coins_test node)
add_functional_test_snippet(double_spending_test node)
add_functional_test_snippet(double_spending_2_tx_test node)
add_functional_test_snippet(try_spend_unknown_and_known_coins_test node)
add_functional_test_snippet(experiment_with_sieze_tx_test node)
add_functional_test_snippet(spam_valid_tx_test node)
