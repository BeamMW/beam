add_test_snippet(status_server_test explorer)
add_test_snippet(adapter_test explorer)
add_dependencies(adapter_test wallet)
target_link_libraries(adapter_test wallet)
# ~ etc
