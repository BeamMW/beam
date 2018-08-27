set(CORE_SRC
    ecc.cpp
    aes.cpp
    common.cpp
    block_crypt.cpp
    navigator.cpp
    storage.cpp
    merkle.cpp
    proto.cpp
# ~etc
)

add_library(core STATIC ${CORE_SRC})

add_dependencies(core p2p pow)
target_link_libraries(core p2p pow)

target_include_directories(core PUBLIC ../secp256k1-zkp/src)

include_directories(${PROJECT_SOURCE_DIR}/core)

add_subdirectory(unittest)
