cmake_minimum_required(VERSION 3.11)

add_library(pbkdf STATIC
   hmac_sha512.c
   pkcs5_pbkdf2.c
   sha512.c
   zeroize.c
   crypto/keccak256.c
)

add_subdirectory(secp256k1-zkp)
add_subdirectory(ethash)
if (BEAM_ATOMIC_SWAP_SUPPORT)
    add_subdirectory(libbitcoin)
endif()
add_subdirectory(sqlite)
add_subdirectory(crypto/blake)
add_subdirectory(libuv)

if(BEAM_IPFS_SUPPORT)
    add_subdirectory(asio-ipfs)
endif()

#add_library(3rdparty INTERFACE IMPORTED GLOBAL)
#set_target_properties(3rdparty PROPERTIES
#    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR})

