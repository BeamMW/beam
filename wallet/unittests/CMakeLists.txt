add_test_snippet(wallet_test wallet)
add_test_snippet(keychain_test wallet)

target_include_directories(wallet_test PRIVATE ${PROJECT_SOURCE_DIR}/wallet/keychain/include)
target_include_directories(keychain_test PRIVATE ${PROJECT_SOURCE_DIR}/wallet/keychain/include)
