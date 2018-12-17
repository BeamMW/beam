add_definitions( -DSQLITE_HAS_CODEC )

add_test_snippet(wallet_test wallet)
add_test_snippet(wallet_db_test wallet)
add_test_snippet(wallet_api_test wallet_api_proto)

add_executable(offline offline.cpp)
add_dependencies(offline node wallet)
target_link_libraries(offline node wallet)
