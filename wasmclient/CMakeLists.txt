project(wasmclient)
cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME wasm-client)

if(EMSCRIPTEN)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value")

    add_library(wasmclient STATIC)
    target_sources(wasmclient
        PRIVATE
            wasmclient.cpp
    )

    # add_subdirectory(${PROJECT_SOURCE_DIR}/../3rdparty/secp256k1-zkp secp256k1-zkp)

    add_library(sqlite STATIC ../3rdparty/sqlite/sqlite3.c)
    target_include_directories(sqlite 
        PRIVATE
        "$ENV{OPENSSL_ROOT_DIR}/include"
    )
    set_property(TARGET sqlite PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(sqlite PUBLIC -D_HAVE_SQLITE_CONFIG_H -DSQLITE_HAS_CODEC EMSCRIPTEN)
    target_link_libraries(sqlite PRIVATE "$ENV{OPENSSL_ROOT_DIR}/libcrypto.a")

    add_library(wallet_db STATIC)
    target_sources(wallet_db
        PRIVATE
            # ../wallet/core/common.cpp
            # common_utils.cpp
            # default_peers.cpp
            # wallet.cpp
            # base_transaction.cpp
            # base_tx_builder.cpp
            # simple_transaction.cpp
            # strings_resources.cpp
            # wallet_network.cpp
            # node_network.cpp
            ../wallet/core/wallet_db.cpp
            # base58.cpp
            # bbs_miner.cpp
            # version.cpp
            # exchange_rate.cpp
            # assets_utils.cpp
        PUBLIC
            # ../wallet/core/common.h
            # default_peers.h
            # wallet.h
            ../wallet/core/wallet_db.h
            # wallet_network.h
            # simple_transaction.h
            # base_transaction.h
            # private_key_keeper.h
            # private_key_keeper.cpp
    )
    
    # target_link_libraries(wallet 
    #     PRIVATE
    #         sqlite
    # )
    # add_subdirectory(${PROJECT_SOURCE_DIR}/../wallet/core wallet)
    target_include_directories(wallet_db 
        PRIVATE
        "${PROJECT_SOURCE_DIR}/.."
        "${PROJECT_SOURCE_DIR}/../3rdparty"
        "${PROJECT_SOURCE_DIR}/../3rdparty/libuv/include"
        $ENV{BOOST_ROOT}
    )
    target_link_libraries(wallet_db PRIVATE sqlite)
    # set_target_properties(wallet_db PROPERTIES LINK_FLAGS "--bind -s WASM=1 -s ERROR_ON_UNDEFINED_SYMBOLS=0 -s MODULARIZE=1 -s USE_BOOST_HEADERS=1")

    add_subdirectory(${PROJECT_SOURCE_DIR}/../wallet/core wallet_db)
    add_subdirectory(${PROJECT_SOURCE_DIR}/../wallet/client wallet_client)
    target_include_directories(wallet_client PUBLIC 
        "${PROJECT_SOURCE_DIR}/.." 
        "${PROJECT_SOURCE_DIR}/../3rdparty" 
        "${PROJECT_SOURCE_DIR}/../3rdparty/libuv/include"
        $ENV{BOOST_ROOT}
    )
        set_target_properties(wallet_client PROPERTIES LINK_FLAGS "--bind -s WASM=1 -s ERROR_ON_UNDEFINED_SYMBOLS=0 -s MODULARIZE=1 -s USE_BOOST_HEADERS=1")

    target_compile_definitions(wasmclient PUBLIC EMSCRIPTEN)
    target_link_libraries(wasmclient PRIVATE wallet_client wallet_db sqlite)
    target_include_directories(wasmclient PUBLIC 
        "${PROJECT_SOURCE_DIR}/../3rdparty" 
        $ENV{BOOST_ROOT})

    add_executable(${TARGET_NAME} wasmclient.cpp)
    target_link_libraries(${TARGET_NAME} PRIVATE wasmclient)

    set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS "--bind -s WASM=1 -s ERROR_ON_UNDEFINED_SYMBOLS=0 -s MODULARIZE=1 -s USE_BOOST_HEADERS=1")
endif()
