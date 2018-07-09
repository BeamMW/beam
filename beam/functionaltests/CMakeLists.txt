set(TEST_TOOLS_SRC
	tools/base_node_connection.h
	tools/base_node_connection.cpp
	tools/tx_generator.h
	tools/tx_generator.cpp
)

function(add_functional_test_snippet EXE_NAME LIB_NAME)
    add_executable(${EXE_NAME} ${EXE_NAME}.cpp ${TEST_TOOLS_SRC})
    add_dependencies(${EXE_NAME} ${LIB_NAME})
    target_link_libraries(${EXE_NAME} ${LIB_NAME})
	if (Boost_FOUND)
		find_package(Boost COMPONENTS program_options)
		target_link_libraries(${EXE_NAME} Boost::program_options)
	else()
		target_link_libraries(${EXE_NAME} boost_program_options)
	endif()
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
