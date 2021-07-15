
set(BEAM_SHADER_CLANG_OPTIONS
        -O3 
        -Os
        -std=c++17 
        -flto
        -fno-rtti 
        -fno-exceptions
        -nostdlib 
        -Wl,--export-dynamic,--no-entry,--allow-undefined)

function(add_shader kind shaders_dir)
    set(WASM_FILE_PATH ${shaders_dir}/${kind}.wasm)

    set(SHADER_ADDITIONAL_DEP )
    if(kind STREQUAL "contract")
        set(SHADER_ADDITIONAL_DEP ${shaders_dir}/${kind}.h)
    endif()

    if(ARGN GREATER 3)
        set(target_name ${ARGV3})
    else()
        set(target_name ${kind}_target)
    endif()

    add_custom_command(
        OUTPUT ${WASM_FILE_PATH}
        COMMAND clang 
                --target=wasm32
                -I ${beam_SOURCE_DIR}/bvm
                ${BEAM_SHADER_CLANG_OPTIONS} 
                ${shaders_dir}/${kind}.cpp 
                --output ${WASM_FILE_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy ${WASM_FILE_PATH} ${kind}.wasm
        COMMENT "Building shader..."
        DEPENDS ${shaders_dir}/${kind}.cpp ${SHADER_ADDITIONAL_DEP}
        VERBATIM
    )
    add_custom_target(
        ${target_name} 
        ALL
        DEPENDS ${WASM_FILE_PATH}
    )
    if(ARGN GREATER 2)
        add_dependencies(
            ARGV2
            ${target_name}
        )
    endif()

endfunction()