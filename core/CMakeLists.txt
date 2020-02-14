cmake_minimum_required(VERSION 3.13)

set(CORE_SRC
    uintBig.cpp
    ecc.cpp
    ecc_min.c
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
    lelantus.cpp
    proto.cpp
    peer_manager.cpp
    fly_client.cpp
    treasury.cpp
    shielded.cpp
# ~etc
)

add_library(core STATIC ${CORE_SRC})
target_link_libraries(core 
    PUBLIC
        Boost::boost
        p2p
    PRIVATE
        pow
        pbkdf
        secp256k1
)

#target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/core)

add_subdirectory(unittest)
