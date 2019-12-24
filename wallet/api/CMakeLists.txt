cmake_minimum_required(VERSION 3.13)

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")

add_library(wallet_api_proto STATIC api.cpp)

target_link_libraries(wallet_api_proto
    PUBLIC 
        wallet 
        utility 
        http
)

if(BEAM_ATOMIC_SWAP_SUPPORT)
    target_compile_definitions(wallet_api_proto PUBLIC BEAM_ATOMIC_SWAP_SUPPORT)
    target_link_libraries(wallet_api_proto PRIVATE swap)
endif()

set(WALLET_API_NAME wallet-api${BEAM_TARGET_SUFFIX})

add_executable(${WALLET_API_NAME} api_cli.cpp)
target_include_directories(${WALLET_API_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${WALLET_API_NAME}
    PRIVATE 
        wallet_api_proto
        cli
)

if(LINUX)
    target_link_libraries(${WALLET_API_NAME} 
        PRIVATE
            -static-libstdc++
            -static-libgcc)
endif()

#add_subdirectory(unittests)

if (BEAM_SIGN_PACKAGE AND WIN32)
    add_custom_command(
        TARGET ${WALLET_API_NAME} POST_BUILD
        COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} "${CMAKE_CURRENT_BINARY_DIR}\\${WALLET_API_NAME}.exe"
    )
endif()
