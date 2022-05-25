project(wasmclient)
cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME wasm-client)

if(EMSCRIPTEN)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value \
                                            -pthread \
                                            -Wno-pthreads-mem-growth \
                                            -Wno-deprecated-declarations \
                                            -Wcast-align \
                                            -Wno-unused-but-set-variable \
                                            -Wno-limited-postlink-optimizations")
    add_executable(${TARGET_NAME}
        common.cpp
        wasmclient.cpp
        wasm_beamapi.h
        wasm_beamapi.cpp
    )
    target_link_libraries(${TARGET_NAME} PRIVATE 
        wallet_client_no_ipfs
        mnemonic
        wallet_api_no_ipfs
    )
    if (BEAM_MAINNET)
        set(BEAM_NETWORK_NAME "mainnet")
    else()
        set(BEAM_NETWORK_NAME ${BEAM_SUFFIX_LOWER_CASE})
    endif()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/package.json.in" "${CMAKE_CURRENT_BINARY_DIR}/package.json")
   # configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../License" "${CMAKE_CURRENT_BINARY_DIR}/License" COPYONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_BINARY_DIR}/README.md" COPYONLY)

    set_target_properties(${TARGET_NAME}
        PROPERTIES COMPILE_FLAGS "-s PROXY_TO_PTHREAD=0 \
                                  -s USE_PTHREADS=1 \
                                  -s WASM_WORKERS=0 \
                                  -s DISABLE_EXCEPTION_CATCHING=0 \
                                  -s STRICT=0"
        LINK_FLAGS " \
                   --bind --no-entry -v \
                   -s FORCE_FILESYSTEM=1 \
                   -s WEBSOCKET_URL=wss:// \
                   -s WASM_BIGINT=0 \
                   -s STRICT=0 \
                   -s DEMANGLE_SUPPORT=0 \
                   -s PTHREADS_DEBUG=0 \
                   -s SAFE_HEAP=0 \
                   -s SOCKET_DEBUG=0 \
                   -s DISABLE_EXCEPTION_CATCHING=0 \
                   -s VERBOSE=1 \
                   -s USE_PTHREADS=1 \
                   -s WASM_WORKERS=0 \
                   -s WASM=1 \
                   -s ERROR_ON_UNDEFINED_SYMBOLS=1 \
                   -s MODULARIZE=1 \
                   -s ALLOW_BLOCKING_ON_MAIN_THREAD=0 \
                   -s INITIAL_MEMORY=100MB \
                   -s ALLOW_MEMORY_GROWTH=0 \
                   -s DYNAMIC_EXECUTION=0 \
                   -s USE_BOOST_HEADERS=1 \
                   -s ASSERTIONS=1 \
                   -s PTHREAD_POOL_SIZE='window.navigator.hardwareConcurrency < ${BEAM_WEB_WALLET_THREADS_NUM} ? \
                                                            window.navigator.hardwareConcurrency : ${BEAM_WEB_WALLET_THREADS_NUM}' \
                   -s EXPORT_NAME='BeamModule' \
                   -s EXPORTED_RUNTIME_METHODS=[\"UTF8ToString\",\"FS\"] \
                   -s WEBSOCKET_SUBPROTOCOL='null' \
                   -lidbfs.js"
    )
endif()
