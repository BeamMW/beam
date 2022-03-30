cmake_minimum_required(VERSION 3.13)

add_library(wallet_ipfs STATIC
    ipfs_imp.cpp
    ipfs_async.cpp
)

target_link_libraries(wallet_ipfs
    PUBLIC
        Boost::boost
        Boost::coroutine
    PRIVATE
        utility
        asio-ipfs
)

target_include_directories(wallet_ipfs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(wallet_ipfs PUBLIC BEAM_IPFS_SUPPORT)