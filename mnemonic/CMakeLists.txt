set(MNEMONIC_SRC
    dictionary.cpp
    mnemonic.cpp

    external/hmac_sha512.c
    external/pkcs5_pbkdf2.c
    external/sha512.c
    external/zeroize.c
)

add_library(mnemonic STATIC ${MNEMONIC_SRC})
add_dependencies(mnemonic core)
target_link_libraries(mnemonic core )

add_subdirectory(unittests)
