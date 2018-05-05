set(WALLET_SRC
    wallet.h
    wallet.cpp
    sender.cpp
    receiver.cpp
)

add_subdirectory(sqlite)

add_library(wallet STATIC ${WALLET_SRC})

if (Boost_FOUND)
	find_package(Boost COMPONENTS filesystem)
    target_link_libraries(wallet Boost::filesystem)
else()
    target_link_libraries(wallet boost_filesystem)
endif()

add_dependencies(wallet core sqlite)
target_link_libraries(wallet core sqlite)

add_subdirectory(unittests)
