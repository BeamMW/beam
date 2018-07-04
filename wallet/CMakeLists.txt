add_definitions( -DSQLITE_HAS_CODEC )

set(WALLET_SRC
    wallet.h
    wallet.cpp
    negotiator.cpp
    wallet_network.cpp
    wallet_db.cpp
)

add_library(wallet STATIC ${WALLET_SRC})
add_dependencies(wallet core p2p sqlite)
target_link_libraries(wallet core p2p sqlite)

if (Boost_FOUND)
	find_package(Boost COMPONENTS filesystem)
    target_link_libraries(wallet Boost::filesystem)
else()
    target_link_libraries(wallet boost_filesystem)
    target_link_libraries(wallet boost_system)
endif()

add_subdirectory(unittests)
