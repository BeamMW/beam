project(wasmclient)
cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME wasm-client)

if(EMSCRIPTEN)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value -pthread -Wno-pthreads-mem-growth -Wno-deprecated-declarations -Wcast-align")
    add_executable(${TARGET_NAME}
        common.cpp
        wasmclient.cpp
        wasm_beamapi.h
        wasm_beamapi.cpp)
    target_link_libraries(${TARGET_NAME} PRIVATE 
        wallet_client
        mnemonic
        wallet_api
    )

    set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_FLAGS "-s USE_PTHREADS=1 -s DISABLE_EXCEPTION_CATCHING=0 -s STRICT=0" 
        LINK_FLAGS "--bind --no-entry -s WASM_BIGINT=0 -s STRICT=0 -s DEMANGLE_SUPPORT=0 -s PTHREADS_DEBUG=0 -s SAFE_HEAP=0  -v -s SOCKET_DEBUG=0 -s DISABLE_EXCEPTION_CATCHING=0 -s VERBOSE=1 -s USE_PTHREADS=1 -s WASM=1 -s ERROR_ON_UNDEFINED_SYMBOLS=1 -s MODULARIZE=1 -s ALLOW_BLOCKING_ON_MAIN_THREAD=0 -s ALLOW_MEMORY_GROWTH=1 -s USE_BOOST_HEADERS=1 -s ASSERTIONS=1 -s PTHREAD_POOL_SIZE='window.navigator.hardwareConcurrency < ${BEAM_WEB_WALLET_THREADS_NUM} ? window.navigator.hardwareConcurrency : ${BEAM_WEB_WALLET_THREADS_NUM}' -s EXPORT_NAME='BeamModule' -s WEBSOCKET_SUBPROTOCOL='null' -lidbfs.js -lworkerfs.js")
endif()
