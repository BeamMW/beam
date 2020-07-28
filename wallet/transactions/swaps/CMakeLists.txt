cmake_minimum_required(VERSION 3.13)

set(SWAP_SRC
    common.cpp
    swap_transaction.cpp
    lock_tx_builder.cpp
    shared_tx_builder.cpp
    utils.cpp
    swap_tx_description.cpp
    bridges/bitcoin/client.cpp
    bridges/bitcoin/common.cpp
    bridges/bitcoin/bitcoin_side.cpp
    bridges/bitcoin/bitcoin_core_016.cpp
    bridges/bitcoin/bitcoin_core_017.cpp
    bridges/bitcoin/electrum.cpp
    bridges/bitcoin/settings.cpp
    bridges/bitcoin/settings_provider.cpp
    bridges/bitcoin_cash/common.cpp
    bridges/bitcoin_cash/bitcoin_cash_core.cpp
    bridges/bitcoin_cash/bitcoin_cash_side.cpp
    bridges/bitcoin_sv/common.cpp
    bridges/bitcoin_sv/bitcoin_sv_core.cpp
    #bridges/bitcoin_sv/bitcoin_sv_side.cpp
    bridges/dogecoin/common.cpp
    bridges/dogecoin/dogecoin_core_014.cpp
    bridges/dogecoin/dogecoin_side.cpp
    bridges/dash/common.cpp
    bridges/dash/dash_core_014.cpp
    bridges/dash/dash_side.cpp
    bridges/litecoin/common.cpp
    bridges/litecoin/litecoin_core_016.cpp
    bridges/litecoin/litecoin_core_017.cpp
    bridges/litecoin/litecoin_side.cpp
    bridges/qtum/common.cpp
    bridges/qtum/qtum_core_017.cpp
    bridges/qtum/qtum_side.cpp
)

add_library(swap STATIC ${SWAP_SRC})
target_link_libraries(swap PUBLIC wallet libbitcoin PRIVATE http)
target_compile_definitions(swap PUBLIC BEAM_ATOMIC_SWAP_SUPPORT)
target_include_directories(swap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")



