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
)

if (NOT BEAM_WALLET_CLIENT_LIBRARY)
    add_subdirectory(ethash_service)
    add_subdirectory(sid_generator)
endif()

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittest)
endif()
