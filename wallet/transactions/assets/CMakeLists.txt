cmake_minimum_required(VERSION 3.13)

add_library(assets STATIC)
target_sources(assets
    PRIVATE
        aissue_transaction.cpp
        aregister_transaction.cpp
        aunregister_transaction.cpp
        ainfo_transaction.cpp
        assets_reg_creators.cpp
        asset_base_tx.cpp
)
target_compile_definitions(assets PUBLIC BEAM_CONFIDENTIAL_ASSETS_SUPPORT)
target_link_libraries(assets 
    PUBLIC
        wallet_core
)
