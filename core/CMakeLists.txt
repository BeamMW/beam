cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

set(CORE_SRC
    ecc.cpp
    block_crypt.cpp
    navigator.cpp
    storage.cpp
# ~etc
)

add_library(core STATIC ${CORE_SRC})
target_include_directories(core PUBLIC ../secp256k1-zkp/src)

include_directories(${PROJECT_SOURCE_DIR}/core)

# add_subdirectory(unittest)
