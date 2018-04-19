set(CORE_SRC
    ecc.cpp
    block_crypt.cpp
    navigator.cpp
    storage.cpp
# ~etc
)
add_library(core STATIC ${CORE_SRC})
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/secp256k1-zkp/src)

include_directories(${PROJECT_SOURCE_DIR}/core)

add_subdirectory(unittest)
