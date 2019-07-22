cmake_minimum_required(VERSION 3.5)

set(SWAP_SRC
    swap_transaction.cpp
    lock_tx_builder.cpp
    shared_tx_builder.cpp
    ../bitcoin/bitcoin_side.cpp
    ../bitcoin/bitcoind016.cpp
    ../bitcoin/bitcoind017.cpp
    ../bitcoin/bitcoin_settings.cpp
    ../litecoin/litecoind016.cpp
    ../litecoin/litecoind017.cpp
    ../litecoin/litecoin_side.cpp
    ../qtum/qtumd017.cpp
    ../qtum/qtum_side.cpp
)

add_library(swap STATIC ${SWAP_SRC})
target_link_libraries(swap PUBLIC wallet libbitcoin PRIVATE http)
target_include_directories(swap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")



