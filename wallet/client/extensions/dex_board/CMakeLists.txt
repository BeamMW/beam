cmake_minimum_required(VERSION 3.13)

add_library(dex_board STATIC)
target_sources(dex_board
    PUBLIC
        dex_order.h
        dex_board.h
    PRIVATE
        dex_order.cpp
        dex_board.cpp
)

target_link_libraries(dex_board 
    PUBLIC
        broadcast_gateway
        wallet
)

target_compile_features(dex_board PUBLIC cxx_std_17)

