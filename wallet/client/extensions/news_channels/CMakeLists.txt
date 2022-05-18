cmake_minimum_required(VERSION 3.13)

add_library(news_channels STATIC)
target_sources(news_channels
    PUBLIC
        verification_provider.h
        verification_info.h
        updates_provider.h
        wallet_updates_provider.h
        version_info.h
        exchange_rate_provider.h
        interface.h
    PRIVATE
        verification_provider.cpp
        updates_provider.cpp
        wallet_updates_provider.cpp
        version_info.cpp
        exchange_rate_provider.cpp
)

target_link_libraries(news_channels 
    PUBLIC
        broadcast_gateway
        wallet_core
)

target_compile_features(news_channels PUBLIC cxx_std_17)

