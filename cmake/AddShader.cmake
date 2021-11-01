
set(BEAM_SHADER_CLANG_OPTIONS
        -O3 
        #-Os
        -std=c++17 
        #-flto
        -fno-rtti 
        #-fno-exceptions
        -nostdlib 
        -Wl,--export-dynamic,--no-entry,--allow-undefined)

function(add_shader kind shaders_dir)
    set(WASM_FILE_PATH ${shaders_dir}/${kind}.wasm)

    set(SHADER_ADDITIONAL_DEP )
    if(kind STREQUAL "contract")
        set(SHADER_ADDITIONAL_DEP ${shaders_dir}/${kind}.h)
    endif()
    if(ARGC GREATER 2)
        set(target_name ${ARGV2})
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

endfunction()


function(build_target target_name)
    add_custom_target(${target_name}_build ALL
        COMMAND ${CMAKE_COMMAND} 
                                    -G "Ninja" 
                                    -DCMAKE_BUILD_TYPE:STRING=MinSizeRel
                                    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${WASI_SDK_PREFIX}/share/cmake/wasi-sdk.cmake
                                    -DBEAM_SHADER_SDK=${BEAM_SHADER_SDK}
                                    -DCMAKE_SYSROOT=${WASI_SDK_PREFIX}/share/wasi-sysroot
                                    -DWASI_SDK_PREFIX=${WASI_SDK_PREFIX}
                                    -DCMAKE_CXX_COMPILER_FORCED=True
                                    -DCMAKE_C_COMPILER_FORCED=True 
                                    -S ${PROJECT_SOURCE_DIR}
                                    -B ${PROJECT_BINARY_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} --build ${PROJECT_BINARY_DIR}/shaders --parallel --target ${target_name}  # --config ${BEAM_SIDGEN_BUILD_TYPE}
        COMMENT "Building target ${target_name} ..."
        VERBATIM
    )
endfunction()