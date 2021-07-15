
set(BEAM_SIDGEN_BUILD_TYPE Release)

add_custom_target (generate_sid_target
    COMMAND ${CMAKE_COMMAND} 
                                -DCMAKE_BUILD_TYPE=Release
                                -DBEAM_SHADER_TESTS_ENABLED=TRUE
                                ${PROJECT_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --parallel --target generate-sid --config ${BEAM_SIDGEN_BUILD_TYPE}
    VERBATIM
)

function(generate_sid_header contract_target)
    add_custom_target(${contract_target}_header
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/beam/bvm/sid_generator/${BEAM_SIDGEN_BUILD_TYPE}/generate-sid $<TARGET_FILE:${contract_target}> > $<TARGET_PROPERTY:${contract_target},SOURCE_DIR>/contract_sid.i
        #COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${contract_target}> $<TARGET_PROPERTY:${contract_target},SOURCE_DIR>/$<TARGET_FILE_NAME:${contract_target}>
        COMMENT "Generating SID..."
        DEPENDS generate_sid_target
        VERBATIM
    )
endfunction()

macro(copy_shader shader_target)
    add_custom_target (copy_${shader_target} ALL
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${shader_target}> $<TARGET_PROPERTY:${shader_target},SOURCE_DIR>/$<TARGET_FILE_NAME:${shader_target}>
        COMMENT "Copying shader..."
        VERBATIM
    )
endmacro()
