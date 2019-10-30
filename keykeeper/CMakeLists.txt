project(keykeeper)
cmake_minimum_required(VERSION 3.11)

set(TARGET_NAME wasm-key-keeper)

if(EMSCRIPTEN)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_library(keykeeper STATIC)

target_sources(keykeeper
    PRIVATE
        local_private_key_keeper.cpp
)

if(BEAM_HW_WALLET)
    target_sources(keykeeper PRIVATE trezor_key_keeper.cpp)
endif()

if(EMSCRIPTEN)

    target_link_libraries(keykeeper PRIVATE secp256k1)
    target_include_directories(keykeeper PUBLIC 
        "${PROJECT_SOURCE_DIR}/.." 
        "${PROJECT_SOURCE_DIR}/../3rdparty" 
        "${PROJECT_SOURCE_DIR}/../3rdparty/libuv/include"
        $ENV{BOOST_ROOT})
    
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value")

    add_executable(${TARGET_NAME} wasm_key_keeper.cpp)
    target_link_libraries(${TARGET_NAME} PRIVATE keykeeper)

    add_subdirectory(${PROJECT_SOURCE_DIR}/../3rdparty/secp256k1-zkp secp256k1-zkp)

    target_sources(keykeeper
        PRIVATE
            ${PROJECT_SOURCE_DIR}/../core/uintBig.cpp
            ${PROJECT_SOURCE_DIR}/../core/ecc.cpp
            ${PROJECT_SOURCE_DIR}/../core/ecc_bulletproof.cpp
            ${PROJECT_SOURCE_DIR}/../core/block_crypt.cpp
            ${PROJECT_SOURCE_DIR}/../core/block_rw.cpp
            ${PROJECT_SOURCE_DIR}/../core/merkle.cpp
            ${PROJECT_SOURCE_DIR}/../core/block_validation.cpp
            ${PROJECT_SOURCE_DIR}/../core/aes.cpp
            ${PROJECT_SOURCE_DIR}/../utility/common.cpp

            ${PROJECT_SOURCE_DIR}/../mnemonic/dictionary.cpp
            ${PROJECT_SOURCE_DIR}/../mnemonic/mnemonic.cpp

            ${PROJECT_SOURCE_DIR}/../3rdparty/hmac_sha512.c
            ${PROJECT_SOURCE_DIR}/../3rdparty/pkcs5_pbkdf2.c
            ${PROJECT_SOURCE_DIR}/../3rdparty/sha512.c
            ${PROJECT_SOURCE_DIR}/../3rdparty/zeroize.c
            ${PROJECT_SOURCE_DIR}/../3rdparty/utilstrencodings.cpp
    )

    set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS "--bind -s WASM=1 -s ERROR_ON_UNDEFINED_SYMBOLS=0")
else()

    target_link_libraries(keykeeper PRIVATE Boost::boost utility)
    
endif()

if (NOT ANDROID AND NOT IOS)
    if(BEAM_HW_WALLET)

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

        target_link_libraries(keykeeper PRIVATE hw_wallet)

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
endif()