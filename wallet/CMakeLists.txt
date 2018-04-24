set(WALLET_SRC
    wallet.h
    wallet.cpp
    sender.cpp
    receiver.cpp
)

add_library(wallet STATIC ${WALLET_SRC})
add_dependencies(wallet core keychain)
target_link_libraries(wallet core keychain)
add_subdirectory(unittests)
