add_test_snippet(node_test node)

configure_file("../../bvm/Shaders/vault/contract.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault/contract.wasm" COPYONLY)
configure_file("../../bvm/Shaders/vault/app.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault/app.wasm" COPYONLY)
configure_file("../../bvm/Shaders/sidechain_pos/contract_l2.wasm" "${CMAKE_CURRENT_BINARY_DIR}/sidechain_pos/contract_l2.wasm" COPYONLY)
configure_file("../../bvm/Shaders/pbft/pbft_dpos.wasm" "${CMAKE_CURRENT_BINARY_DIR}/pbft/pbft_dpos.wasm" COPYONLY)
configure_file("../../bvm/Shaders/pbft/pbft_stat.wasm" "${CMAKE_CURRENT_BINARY_DIR}/pbft/pbft_stat.wasm" COPYONLY)
configure_file("../../bvm/Shaders/Explorer/Parser.wasm" "${CMAKE_CURRENT_BINARY_DIR}/Explorer/Parser.wasm" COPYONLY)
