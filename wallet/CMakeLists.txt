cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME beam-wallet${BEAM_TARGET_SUFFIX})

add_library(wallet STATIC)
target_sources(wallet
    PRIVATE
        common.cpp
        default_peers.cpp
        wallet.h
        wallet.cpp
        base_transaction.cpp
        base_tx_builder.cpp
        wallet_transaction.cpp
        wallet_network.cpp
        wallet_db.cpp
        wallet_client.h
        wallet_client.cpp
        base58.cpp
        unittests/util.cpp
)

target_link_libraries(wallet 
    PUBLIC
        core
        keykeeper
    PRIVATE
        sqlite
)

target_compile_features(wallet PUBLIC cxx_std_17)

if(BEAM_ATOMIC_SWAP_SUPPORT)
    add_subdirectory(swaps)
endif()

if (NOT ANDROID AND NOT IOS)
    if(BEAM_HW_WALLET)

        target_compile_definitions(wallet PRIVATE BEAM_HW_WALLET)

    endif()

    add_executable(${TARGET_NAME} cli.cpp strings_resources.h strings_resources.cpp)

    if(LINUX)
        target_link_libraries(${TARGET_NAME}
            PRIVATE 
                -static-libstdc++ 
                -static-libgcc)
    endif()

    configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    target_link_libraries(${TARGET_NAME}
        PRIVATE
            wallet
            cli
            mnemonic
    )

    add_library(wallet_api_proto STATIC api.cpp)
    target_link_libraries(wallet_api_proto
        PUBLIC 
            wallet 
            utility 
            http
    )

    if(BEAM_ATOMIC_SWAP_SUPPORT)
        target_compile_definitions(wallet PRIVATE BEAM_ATOMIC_SWAP_SUPPORT)
        target_compile_definitions(${TARGET_NAME} PRIVATE BEAM_ATOMIC_SWAP_SUPPORT)
        target_link_libraries(${TARGET_NAME} PRIVATE swap)
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

    add_subdirectory(unittests)

    if (BEAM_SIGN_PACKAGE AND WIN32)
        add_custom_command(
            TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} "${CMAKE_CURRENT_BINARY_DIR}\\{TARGET_NAME}.exe"
        )
        add_custom_command(
            TARGET ${WALLET_API_NAME} POST_BUILD
            COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} "${CMAKE_CURRENT_BINARY_DIR}\\${WALLET_API_NAME}.exe"
        )
    endif()
endif()
