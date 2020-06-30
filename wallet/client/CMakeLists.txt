cmake_minimum_required(VERSION 3.13)

add_library(wallet_client STATIC)
target_sources(wallet_client
    PUBLIC
        wallet_client.h
    PRIVATE
        wallet_client.cpp
        extensions/broadcast_gateway/broadcast_router.cpp
        extensions/broadcast_gateway/broadcast_msg_creator.cpp
        extensions/broadcast_gateway/broadcast_msg_validator.cpp
        extensions/news_channels/updates_provider.cpp
        extensions/news_channels/wallet_updates_provider.cpp
        extensions/news_channels/version_info.cpp
        extensions/news_channels/exchange_rate_provider.cpp
        extensions/offers_board/swap_offer.cpp
        extensions/offers_board/swap_offer_token.cpp
        extensions/offers_board/offers_protocol_handler.cpp
        extensions/offers_board/swap_offers_board.cpp
        extensions/notifications/notification_center.cpp
)

target_link_libraries(wallet_client 
    PUBLIC
        wallet
)

target_compile_features(wallet_client PUBLIC cxx_std_17)

if(BEAM_ATOMIC_SWAP_SUPPORT)
    target_link_libraries(wallet_client PUBLIC swap)
endif()

if(BEAM_CONFIDENTIAL_ASSETS_SUPPORT)
    target_link_libraries(wallet_client PUBLIC assets)
endif()

if(BEAM_LELANTUS_SUPPORT)
    target_link_libraries(wallet_client PUBLIC lelantus)
endif()
