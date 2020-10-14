add_test_snippet(node_test node)
add_test_snippet(node_1_test node)

configure_file("../../core/Shaders/vault.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault.wasm" COPYONLY)
configure_file("../../core/Shaders/dummy.wasm" "${CMAKE_CURRENT_BINARY_DIR}/dummy.wasm" COPYONLY)
configure_file("../../core/Shaders/oracle.wasm" "${CMAKE_CURRENT_BINARY_DIR}/oracle.wasm" COPYONLY)
