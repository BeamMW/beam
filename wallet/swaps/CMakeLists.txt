cmake_minimum_required(VERSION 3.13)

set(SWAP_SRC
    common.cpp
    swap_transaction.cpp
    lock_tx_builder.cpp
    shared_tx_builder.cpp
    swap_offers_board.cpp
    utils.cpp
    ../bitcoin/client.cpp
    ../bitcoin/common.cpp
    ../bitcoin/bitcoin_side.cpp
    ../bitcoin/bitcoin_core_016.cpp
    ../bitcoin/bitcoin_core_017.cpp
    ../bitcoin/electrum.cpp
    ../bitcoin/settings.cpp
    ../bitcoin/settings_provider.cpp
    ../litecoin/common.cpp
    ../litecoin/litecoin_core_016.cpp
    ../litecoin/litecoin_core_017.cpp
    ../litecoin/litecoin_side.cpp
    ../qtum/common.cpp
    ../qtum/qtum_core_017.cpp
    ../qtum/qtum_side.cpp
)

add_library(swap STATIC ${SWAP_SRC})
target_link_libraries(swap PUBLIC wallet libbitcoin PRIVATE http)
target_include_directories(swap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")



