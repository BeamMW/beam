cmake_minimum_required(VERSION 3.13)

add_library(export STATIC)
target_sources(export
    PUBLIC
        tx_history_to_csv.h
    PRIVATE
        tx_history_to_csv.cpp
)

target_link_libraries(export 
    PUBLIC
        wallet_core
)

if(BEAM_ASSET_SWAP_SUPPORT)
    target_compile_definitions(export PUBLIC BEAM_ASSET_SWAP_SUPPORT)
endif()

if(BEAM_ATOMIC_SWAP_SUPPORT)
    target_compile_definitions(export PUBLIC BEAM_ATOMIC_SWAP_SUPPORT)
endif()

target_compile_features(export PUBLIC cxx_std_17)

