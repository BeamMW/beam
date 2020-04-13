cmake_minimum_required(VERSION 3.13)

add_library(assets STATIC)
target_sources(assets
    PRIVATE
        aissue_tx_builder.cpp
        aissue_transaction.cpp
        aregister_tx_builder.cpp
        aregister_transaction.cpp
        aunregister_tx_builder.cpp
        aunregister_transaction.cpp
        ainfo_transaction.cpp
        assets_reg_creators.cpp
        assets_kdf_utils.cpp
)

target_link_libraries(assets 
    PUBLIC
        wallet
)
