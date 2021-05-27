cmake_minimum_required(VERSION 3.13)

set(SRC
    ethash_utils.cpp
    ethash_service.cpp
)

add_executable(ethash-service ${SRC})
target_link_libraries(ethash-service 
    PRIVATE
        core
        keccak
        ethash
        cli
        http
        utility
        p2p
        wallet_api
)
