add_test_snippet(shaders_test bvm)

configure_file("../Shaders/vault.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vault.wasm" COPYONLY)
configure_file("../Shaders/vaultManager.wasm" "${CMAKE_CURRENT_BINARY_DIR}/vaultManager.wasm" COPYONLY)
configure_file("../Shaders/dummy.wasm" "${CMAKE_CURRENT_BINARY_DIR}/dummy.wasm" COPYONLY)
configure_file("../Shaders/oracle.wasm" "${CMAKE_CURRENT_BINARY_DIR}/oracle.wasm" COPYONLY)
configure_file("../Shaders/StableCoin.wasm" "${CMAKE_CURRENT_BINARY_DIR}/StableCoin.wasm" COPYONLY)
