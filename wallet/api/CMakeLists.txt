cmake_minimum_required(VERSION 3.13)

add_library(wallet_api_proto STATIC
    api_errors.cpp
    api_base.cpp
)

target_link_libraries(wallet_api_proto
    PUBLIC 
        wallet_client
        utility
)

set(API_SRC
    api_handler.cpp
    wallet_api.cpp
)

if(BEAM_ATOMIC_SWAP_SUPPORT)
    set(API_SRC
        ${API_SRC}
        wallet_api_swap.cpp
        swap_client.cpp
        swap_eth_client.cpp
    )
endif()

add_library(wallet_api STATIC ${API_SRC})

target_link_libraries(wallet_api
    PUBLIC 
        wallet 
        wallet_api_proto
)

set(WALLET_API_NAME wallet-api${BEAM_TARGET_SUFFIX})

add_executable(${WALLET_API_NAME} api_cli.cpp api_cli_swap.cpp)
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
