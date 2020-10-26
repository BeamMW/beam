cmake_minimum_required(VERSION 3.13)

set(BVM_SRC
    wasm_interpreter.cpp
    bvm2.cpp
    ManagerStd.cpp
# ~etc
)

add_library(bvm STATIC ${BVM_SRC})
target_link_libraries(bvm 
    PUBLIC
        core
)

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittest)
endif()
