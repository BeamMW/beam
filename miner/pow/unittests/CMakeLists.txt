function(add_snippet EXE_NAME)
    add_executable(${EXE_NAME} ${EXE_NAME}.cpp)
    add_dependencies(${EXE_NAME} pow)
    target_link_libraries(${EXE_NAME} pow)
endfunction()

add_snippet(equihash_test)
