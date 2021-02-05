cmake_minimum_required(VERSION 3.13)

add_library(wallet STATIC)

target_sources(wallet
    PRIVATE
        common.cpp
        common_utils.cpp
        default_peers.cpp
        wallet.cpp
        base_transaction.cpp
        base_tx_builder.cpp
        simple_transaction.cpp
        strings_resources.cpp
        wallet_network.cpp
        node_network.cpp
        wallet_db.cpp
        base58.cpp
        bbs_miner.cpp
        version.cpp
        exchange_rate.cpp
        assets_utils.cpp
    PUBLIC
        common.h
        default_peers.h
        wallet.h
        wallet_db.h
        wallet_network.h
        simple_transaction.h
        base_transaction.h
        private_key_keeper.h
        private_key_keeper.cpp
)

target_link_libraries(wallet 
    PUBLIC
        core
        keykeeper
        node
    PRIVATE
        sqlite
)

target_compile_features(wallet PUBLIC cxx_std_17)


if (NOT ANDROID AND NOT IOS)

    if(BEAM_HW_WALLET)
        target_compile_definitions(wallet PUBLIC BEAM_HW_WALLET)
    endif()

    if(BEAM_LASER_SUPPORT)
        target_compile_definitions(wallet PUBLIC BEAM_LASER_SUPPORT)
    endif()

    #add_subdirectory(unittests)

endif()
