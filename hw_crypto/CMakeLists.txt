cmake_minimum_required(VERSION 3.13)

set(HW_CRYPTO_SRC
    hw_crypto.c
    context.c
# ~etc
)

add_library(hw_crypto STATIC ${HW_CRYPTO_SRC})
target_link_libraries(hw_crypto 
    PRIVATE
        secp256k1
)

target_include_directories(hw_crypto PUBLIC ${PROJECT_SOURCE_DIR}/hw_crypto)

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittest)
endif()
