cmake_minimum_required(VERSION 3.13)

add_library(wallet_api_proto STATIC
    api_base.cpp
    api.cpp
)

target_link_libraries(wallet_api_proto
    PUBLIC 
        wallet_client
        utility
)

add_library(wallet_api STATIC api_handler.cpp)

target_link_libraries(wallet_api
    PUBLIC 
        wallet 
        wallet_api_proto
)

set(WALLET_API_NAME wallet-api${BEAM_TARGET_SUFFIX})

add_executable(${WALLET_API_NAME} api_cli.cpp)
target_link_libraries(${WALLET_API_NAME}
    PRIVATE 
        wallet_api_proto
        wallet_api
        http
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
