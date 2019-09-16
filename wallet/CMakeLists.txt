cmake_minimum_required(VERSION 3.11)

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
        local_private_key_keeper.cpp
        base58.cpp
        unittests/util.cpp
)

if(BEAM_HW_WALLET)
    target_sources(wallet PRIVATE trezor_key_keeper.cpp)
endif()

target_link_libraries(wallet 
    PUBLIC
        core
    PRIVATE
        sqlite
)

target_compile_features(wallet PUBLIC cxx_std_17)

if(BEAM_ATOMIC_SWAP_SUPPORT)
    add_subdirectory(swaps)
endif()

if (NOT ANDROID)
    if(BEAM_HW_WALLET)

        target_compile_definitions(wallet PRIVATE BEAM_HW_WALLET)

        add_library(hw_wallet STATIC hw_wallet.cpp
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-common.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-management.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-beam.pb.cc
        )

        target_link_libraries(hw_wallet
            PUBLIC 
                core
                libprotobuf
                libcurl
        )

        if(MSVC)
            target_compile_options(hw_wallet PRIVATE "/wd4125")
            target_compile_options(hw_wallet PRIVATE "/wd4127")
            target_compile_options(hw_wallet PRIVATE "/wd4244")
            target_compile_options(hw_wallet PRIVATE "/wd4996")
        endif()

        target_include_directories(hw_wallet PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages
            ${PROJECT_SOURCE_DIR}/3rdparty/curl/include
            ${PROJECT_SOURCE_DIR}/3rdparty/protobuf/src)

        target_link_libraries(wallet PRIVATE hw_wallet)

        # build trezor usage demo
        add_executable(trezor-cpp-client ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/main.cpp)
        if(MSVC)
            target_compile_options(trezor-cpp-client PRIVATE "/wd4125")
            target_compile_options(trezor-cpp-client PRIVATE "/wd4127")
            target_compile_options(trezor-cpp-client PRIVATE "/wd4244")
            target_compile_options(trezor-cpp-client PRIVATE "/wd4996")
        endif()

        target_include_directories(trezor-cpp-client PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages)

        target_link_libraries(trezor-cpp-client PRIVATE hw_wallet)

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
