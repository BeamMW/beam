cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME beam-wallet${BEAM_TARGET_SUFFIX})

add_definitions( -DSQLITE_HAS_CODEC )
add_definitions(-DWITH_ICU)

set(WALLET_SRC
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

add_library(wallet STATIC ${WALLET_SRC})
target_link_libraries(wallet core sqlite)

target_compile_features(wallet PUBLIC cxx_std_17)

if (NOT ANDROID)

    if(BEAM_HW_WALLET)

        target_compile_definitions(wallet PRIVATE BEAM_HW_WALLET)

        add_library(hw_wallet STATIC hw_wallet.cpp
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-common.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-management.pb.cc
            ${PROJECT_SOURCE_DIR}/3rdparty/trezor-cpp-client/src/messages/messages-beam.pb.cc
        )

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

        target_link_libraries(wallet hw_wallet libprotobuf libcurl)

    endif()

    add_executable(${TARGET_NAME} cli.cpp)

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)

if(LINUX)
	target_link_libraries(${TARGET_NAME} -static-libstdc++ -static-libgcc)
endif()

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    target_link_libraries(${TARGET_NAME} wallet utility cli swap)

add_library(wallet_api_proto STATIC api.cpp)
target_link_libraries(wallet_api_proto wallet utility http)

set(WALLET_API_NAME wallet-api${BEAM_TARGET_SUFFIX})

add_executable(${WALLET_API_NAME} api_cli.cpp)
target_include_directories(${WALLET_API_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(${WALLET_API_NAME} wallet utility wallet_api_proto cli)

if(LINUX)
    target_link_libraries(${WALLET_API_NAME} -static-libstdc++ -static-libgcc)
endif()

add_subdirectory(unittests)
add_subdirectory(swaps)

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
