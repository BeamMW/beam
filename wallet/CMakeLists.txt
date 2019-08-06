set(TARGET_NAME beam-wallet${BEAM_TARGET_SUFFIX})

add_library(wallet STATIC ${WALLET_SRC})

target_compile_definitions(
    wallet
    PRIVATE SQLITE_HAS_CODEC
)

target_sources(
    wallet
    PRIVATE common.cpp
    PRIVATE default_peers.cpp
    PRIVATE wallet.h
    PRIVATE wallet.cpp
    PRIVATE base_transaction.cpp
    PRIVATE base_tx_builder.cpp
    PRIVATE wallet_transaction.cpp
    PRIVATE wallet_network.cpp
    PRIVATE wallet_db.cpp
    PRIVATE wallet_client.h
    PRIVATE wallet_client.cpp
    PRIVATE swaps/swap_transaction.cpp
    PRIVATE swaps/lock_tx_builder.cpp
    PRIVATE swaps/shared_tx_builder.cpp
    PRIVATE swaps/swap_offers_monitor.cpp
    PRIVATE bitcoin/bitcoin_side.cpp
    PRIVATE bitcoin/bitcoind016.cpp
    PRIVATE bitcoin/bitcoind017.cpp
    PRIVATE litecoin/litecoind016.cpp
    PRIVATE litecoin/litecoind017.cpp
    PRIVATE qtum/qtumd017.cpp
    PRIVATE secstring2.cpp
    PRIVATE unittests/util.cpp
)

add_dependencies(wallet core sqlite libbitcoin http)

target_link_libraries(wallet core sqlite libbitcoin http)

target_include_directories(wallet PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/libbitcoin/include")

if (NOT ANDROID)

    if(BEAM_HW_WALLET)

        target_compile_definitions(wallet PRIVATE BEAM_HW_WALLET)

        add_library(hw_wallet STATIC hw_wallet.cpp
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-common.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-management.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-beam.pb.cc
        )

        add_dependencies(hw_wallet core)
        target_link_libraries(hw_wallet core)

        if(MSVC)
            target_compile_options(hw_wallet PRIVATE "/wd4125")
            target_compile_options(hw_wallet PRIVATE "/wd4127")
            target_compile_options(hw_wallet PRIVATE "/wd4244")
            target_compile_options(hw_wallet PRIVATE "/wd4996")
        endif()

        target_include_directories(hw_wallet PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client)
        target_include_directories(hw_wallet PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src)
        target_include_directories(hw_wallet PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages)
        target_include_directories(hw_wallet PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/curl/include)
        target_include_directories(hw_wallet PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/protobuf/src)
        add_dependencies(wallet hw_wallet libprotobuf libcurl)
        target_link_libraries(wallet hw_wallet libprotobuf libcurl)

    endif()

    add_executable(${TARGET_NAME} cli.cpp)

    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)

    if(LINUX)
        target_link_libraries(${TARGET_NAME} -static-libstdc++ -static-libgcc)
    endif()

    configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    add_dependencies(${TARGET_NAME} wallet utility cli)
    target_link_libraries(${TARGET_NAME} wallet utility cli)

    add_library(wallet_api_proto STATIC api.cpp)
    add_dependencies(wallet_api_proto wallet utility http)
    target_link_libraries(wallet_api_proto wallet utility http)

    set(WALLET_API_NAME wallet-api${BEAM_TARGET_SUFFIX})

    add_executable(${WALLET_API_NAME} api_cli.cpp)
    target_include_directories(${WALLET_API_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        add_dependencies(${WALLET_API_NAME} wallet utility wallet_api_proto cli)
        target_link_libraries(${WALLET_API_NAME} wallet utility wallet_api_proto cli)

    if(LINUX)
        target_link_libraries(${WALLET_API_NAME} -static-libstdc++ -static-libgcc)
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
