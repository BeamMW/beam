add_test_snippet(node_test node)
add_test_snippet(node_1_test node)

configure_file("../../bvm/Shaders/vault.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault.wasm" COPYONLY)
configure_file("../../bvm/Shaders/vaultManager.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vaultManager.wasm" COPYONLY)
