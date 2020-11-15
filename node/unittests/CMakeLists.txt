add_test_snippet(node_test node)
add_test_snippet(node_1_test node)

configure_file("../../bvm/Shaders/vault/contract.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault/contract.wasm" COPYONLY)
configure_file("../../bvm/Shaders/vault/manager.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault/manager.wasm" COPYONLY)
