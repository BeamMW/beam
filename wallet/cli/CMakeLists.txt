cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME beam-wallet)


set(CLI_SRC
    cli.cpp
    utils.cpp
)
if(BEAM_LASER_SUPPORT)
    set(CLI_SRC
        ${CLI_SRC}
        laser.cpp
    )
endif()

if(BEAM_ATOMIC_SWAP_SUPPORT)
    set(CLI_SRC
        ${CLI_SRC}
        swaps.cpp
    )
endif()

if(BEAM_ASSET_SWAP_SUPPORT)
    set(CLI_SRC
        ${CLI_SRC}
        assets_swap.cpp
    )
endif()

add_executable(${TARGET_NAME} ${CLI_SRC})
target_compile_definitions(${TARGET_NAME} PRIVATE APP_NAME=\"${TARGET_NAME}\")

if (BEAM_TEST_SHADERS)

    function (add_shader shader kind)
        add_custom_command(
            OUTPUT ${kind}.wasm
            COMMAND clang 
                    -O0 
                    --target=wasm32
                    -I ${PROJECT_SOURCE_DIR}/bvm 
                    -std=c++17 
                    -fno-rtti
                    -fno-exceptions
                    -g
                    -Wl,--export-dynamic,--no-entry,--allow-undefined -nostdlib 
                    ${PROJECT_SOURCE_DIR}/bvm/Shaders/${shader}/${kind}.cpp --output ${kind}.wasm
            COMMENT "Building shader ${shader}..."
            DEPENDS ${PROJECT_SOURCE_DIR}/bvm/Shaders/${shader}/${kind}.cpp
            VERBATIM
        )
        add_custom_target(
            ${shader}_${kind}_target 
            ALL
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${kind}.wasm
        )
        add_dependencies(
            ${TARGET_NAME}
            ${shader}_${kind}_target
        )

    endfunction()

    add_shader(playground contract)
    add_shader(playground app)
endif()

if(LINUX)
    target_link_libraries(${TARGET_NAME}
        PRIVATE 
            -static-libstdc++ 
            -static-libgcc)
endif()

target_link_libraries(${TARGET_NAME}
    PRIVATE
        wallet
        cli
        mnemonic
)

if(BEAM_LASER_SUPPORT)
    target_link_libraries(${TARGET_NAME} PRIVATE laser)
endif()

if(BEAM_ASSET_SWAP_SUPPORT)
    target_link_libraries(${TARGET_NAME} PRIVATE broadcast_gateway dex_board)
endif()

if (BEAM_SIGN_PACKAGE AND WIN32)
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} "${CMAKE_CURRENT_BINARY_DIR}\\{TARGET_NAME}.exe"
    )
endif()
