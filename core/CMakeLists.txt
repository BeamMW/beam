set(CORE_SRC
    uintBig.cpp
    ecc.cpp
    ecc_bulletproof.cpp
    aes.cpp
    common.cpp
    block_crypt.cpp
    block_rw.cpp
    block_validation.cpp
    chainwork.cpp
    navigator.cpp
    radixtree.cpp
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
