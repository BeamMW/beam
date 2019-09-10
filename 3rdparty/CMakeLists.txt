cmake_minimum_required(VERSION 3.11)

add_library(pbkdf STATIC
   hmac_sha512.c
   pkcs5_pbkdf2.c
   sha512.c
   zeroize.c
)

add_subdirectory(secp256k1-zkp)
add_subdirectory(libbitcoin)
add_subdirectory(sqlite)
add_subdirectory(crypto/blake)

#target_include_directories(pbkdf INTERFACE )
