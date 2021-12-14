cmake_minimum_required(VERSION 3.13)

add_library(ethash_utils STATIC ethash_utils.cpp)
target_link_libraries(ethash_utils 
    PUBLIC 
        core
        keccak
        ethash
        cli
        http
        utility
        p2p
    )

add_executable(ethash-service ethash_service.cpp)
target_link_libraries(ethash-service 
    PRIVATE
        wallet_api_no_ipfs
        ethash_utils
)
