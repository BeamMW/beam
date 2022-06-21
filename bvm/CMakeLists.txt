cmake_minimum_required(VERSION 3.13)

set(BVM_SRC
    wasm_interpreter.cpp
    bvm2.cpp
    invoke_data.cpp
    ManagerStd.cpp
# ~etc
)

add_library(bvm STATIC ${BVM_SRC})
target_link_libraries(bvm 
    PUBLIC
        core
        keccak
        ethash
    PRIVATE
        re2
)

target_include_directories(bvm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT BEAM_WALLET_CLIENT_LIBRARY)
    add_subdirectory(ethash_service)
endif()

if(NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN)
##if (NOT BEAM_WALLET_CLIENT_LIBRARY OR BEAM_BVM_ONLY)
    add_subdirectory(sid_generator)
#endif()
endif()

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittest)
endif()
