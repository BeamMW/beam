cmake_minimum_required(VERSION 3.13)

add_library(swap_offers_board STATIC)
target_sources(swap_offers_board
    PUBLIC
        swap_offer.h
        swap_offer_token.h
        offers_protocol_handler.h
        swap_offers_board.h
        swap_offers_observer.h
    PRIVATE
        swap_offer.cpp
        swap_offer_token.cpp
        offers_protocol_handler.cpp
        swap_offers_board.cpp

)

target_link_libraries(swap_offers_board 
    PUBLIC
        broadcast_gateway
        wallet
)

target_compile_features(swap_offers_board PUBLIC cxx_std_17)

