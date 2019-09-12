cmake_minimum_required(VERSION 3.11)

set(CORE_SRC
    uintBig.cpp
    ecc.cpp
    ecc_bulletproof.cpp
    aes.cpp
    block_crypt.cpp
    block_rw.cpp
    block_validation.cpp
    difficulty.cpp
    chainwork.cpp
    mapped_file.cpp
    navigator.cpp
    radixtree.cpp
    merkle.cpp
    negotiator.cpp
    lightning.cpp
    proto.cpp
    peer_manager.cpp
    fly_client.cpp
    treasury.cpp
# ~etc
)

add_library(core STATIC ${CORE_SRC})

target_link_libraries(core 
    PUBLIC 
        pow 
        p2p
    PRIVATE
        pbkdf
        secp256k1
)

#target_include_directories(core PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/secp256k1-zkp/include)
#target_include_directories(core PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/secp256k1-zkp/src)
#target_include_directories(core PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/)
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/core)

add_subdirectory(unittest)
