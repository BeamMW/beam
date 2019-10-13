cmake_minimum_required(VERSION 3.11)

set(TARGET_NAME key-keeper-test${BEAM_TARGET_SUFFIX})

add_library(keykeeper STATIC)

target_sources(keykeeper
    PRIVATE
        local_private_key_keeper.cpp
)

if(BEAM_HW_WALLET)
    target_sources(keykeeper PRIVATE trezor_key_keeper.cpp)
endif()

target_link_libraries(keykeeper PRIVATE Boost::boost utility)

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")

#add_executable(${TARGET_NAME} main.cpp)
#target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
#target_link_libraries(${TARGET_NAME} PRIVATE Boost::boost keykeeper core mnemonic)

#add_subdirectory(unittest)

if (NOT ANDROID)
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