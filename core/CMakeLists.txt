cmake_minimum_required(VERSION 3.13)

set(CORE_SRC
    uintBig.cpp
    numeric_utils.cpp
    ecc.cpp
    ecc_bulletproof.cpp
    aes.cpp
    aes2.cpp
    base58.cpp
    block_crypt.cpp
    block_rw.cpp
    block_validation.cpp
    difficulty.cpp
    chainwork.cpp
    mapped_file.cpp
    navigator.cpp
    radixtree.cpp
    merkle.cpp
    keccak.cpp
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
        utility
        keccak
    PRIVATE
        pow
        pbkdf
        secp256k1
)

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(core INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittest)
endif()
