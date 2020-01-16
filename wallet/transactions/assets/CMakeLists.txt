cmake_minimum_required(VERSION 3.13)

add_library(assets STATIC)
target_sources(assets
    PRIVATE
        aissue_tx_builder.cpp
        aissue_transaction.cpp
        assets_register.cpp
)

target_link_libraries(assets 
    PUBLIC
        wallet
)
