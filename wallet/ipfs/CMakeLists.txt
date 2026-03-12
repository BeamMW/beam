cmake_minimum_required(VERSION 3.13)

add_library(wallet_ipfs STATIC
    ipfs_imp.cpp
    ipfs_async.cpp
)

target_link_libraries(wallet_ipfs
    PUBLIC
        Boost::boost
        Boost::context
    PRIVATE
        utility
        asio-ipfs
)

# boost::asio::spawn used Boost.Coroutine until Boost 1.86;
# since Boost 1.87 it works only with Boost.Context fibers.
if (Boost_VERSION VERSION_LESS 108700)
    target_link_libraries(wallet_ipfs PUBLIC Boost::coroutine)
endif()

target_include_directories(wallet_ipfs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(wallet_ipfs PUBLIC BEAM_IPFS_SUPPORT)