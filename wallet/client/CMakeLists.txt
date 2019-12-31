cmake_minimum_required(VERSION 3.13)

add_library(wallet_client STATIC)
target_sources(wallet_client
    PRIVATE
        wallet_client.h
        wallet_client.cpp
        extensions/offers_board/swap_offer.cpp
        extensions/offers_board/swap_offer_token.cpp
        extensions/offers_board/offers_protocol_handler.cpp
        extensions/offers_board/swap_offers_board.cpp
        extensions/newscast/newscast.cpp
)

target_link_libraries(wallet_client 
    PUBLIC
        wallet
)

target_compile_features(wallet_client PUBLIC cxx_std_17)
if(BEAM_ATOMIC_SWAP_SUPPORT)
    target_compile_definitions(wallet_client PRIVATE BEAM_ATOMIC_SWAP_SUPPORT)
    target_link_libraries(wallet_client PRIVATE swap)
endif()

