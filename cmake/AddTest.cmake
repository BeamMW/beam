
option(BEAM_TESTS_ENABLED "Enable unit tests" TRUE)
if (BEAM_TESTS_ENABLED)
    enable_testing()

    function(add_test_snippet EXE_NAME LIB_NAME)
        add_executable(${EXE_NAME} ${EXE_NAME}.cpp)
        target_link_libraries(${EXE_NAME} ${LIB_NAME} ${ARGN})
        add_test(NAME ${EXE_NAME} COMMAND $<TARGET_FILE:${EXE_NAME}>)
    endfunction()
endif()
