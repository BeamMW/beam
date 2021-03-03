project(wasmclient)
cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME wasm-client)

if(EMSCRIPTEN)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value -pthread -Wno-pthreads-mem-growth -Wno-deprecated-declarations")

    add_executable(${TARGET_NAME} wasmclient.cpp)
    target_link_libraries(${TARGET_NAME} PRIVATE 
        wallet_client
        mnemonic
        wallet_api_proto
        wallet_api
    )

    set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_FLAGS "-s USE_PTHREADS=1 -s DISABLE_EXCEPTION_CATCHING=0 " LINK_FLAGS "--bind -O3 -v -s SOCKET_DEBUG=0 -s FORCE_FILESYSTEM=1 -s DISABLE_EXCEPTION_CATCHING=0 -s VERBOSE=1 -s USE_PTHREADS=1 -s WASM=1 -s ERROR_ON_UNDEFINED_SYMBOLS=1 -s MODULARIZE=1 -s ALLOW_BLOCKING_ON_MAIN_THREAD=1 -s ALLOW_MEMORY_GROWTH=1 -s USE_BOOST_HEADERS=1 -s ASSERTIONS=1 -s PTHREAD_POOL_SIZE=5 -s EXPORT_NAME='BeamModule' -s WEBSOCKET_SUBPROTOCOL='null'")
endif()
