set(CORE_SRC
    uintBig.cpp
    ecc.cpp
    ecc_bulletproof.cpp
    aes.cpp
    block_crypt.cpp
    block_rw.cpp
    block_validation.cpp
    chainwork.cpp
    navigator.cpp
    radixtree.cpp
    merkle.cpp
    proto.cpp
    peer_manager.cpp
    fly_client.cpp
    treasury.cpp
# ~etc
)

add_library(core STATIC ${CORE_SRC})

add_dependencies(core p2p pow)
target_link_libraries(core p2p pow)

target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/3rdparty/secp256k1-zkp/src)
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/3rdparty)

include_directories(${PROJECT_SOURCE_DIR}/core)

add_subdirectory(unittest)
