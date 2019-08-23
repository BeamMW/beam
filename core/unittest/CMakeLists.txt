add_test_snippet(ecc_test core)
add_test_snippet(storage_test core)

if(BEAM_HW_WALLET)
    target_compile_definitions(ecc_test PRIVATE BEAM_HW_WALLET)
    add_dependencies(ecc_test hw_wallet)
    target_link_libraries(ecc_test hw_wallet)
endif()