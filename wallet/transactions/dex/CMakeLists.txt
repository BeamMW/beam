cmake_minimum_required(VERSION 3.13)

add_library(dex STATIC)
target_sources(dex
    PRIVATE
        dex_tx.cpp
        dex_tx_builder.cpp
)

target_compile_definitions(dex PUBLIC BEAM_CONFIDENTIAL_ASSETS_SUPPORT)
target_link_libraries(dex
    PUBLIC
        wallet
)
