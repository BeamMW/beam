set(WALLET_SRC
    wallet.h
    wallet.cpp
    sender.cpp
    receiver.cpp
)

add_library(wallet STATIC ${WALLET_SRC})
add_dependencies(wallet core)
target_link_libraries(wallet core)
add_subdirectory(unittests)
