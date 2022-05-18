cmake_minimum_required(VERSION 3.13)

add_library(broadcast_gateway STATIC)
target_sources(broadcast_gateway
    PUBLIC
        broadcast_router.h
        broadcast_msg_creator.h
        broadcast_msg_validator.h
        interface.h
    PRIVATE
        broadcast_router.cpp
        broadcast_msg_creator.cpp
        broadcast_msg_validator.cpp
)

target_link_libraries(broadcast_gateway 
    PUBLIC
        wallet_core
)

target_compile_features(broadcast_gateway PUBLIC cxx_std_17)

