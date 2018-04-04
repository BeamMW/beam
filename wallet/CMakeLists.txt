set(WALLET_SRC
    wallet.h
    wallet.cpp
)

add_library(wallet STATIC ${WALLET_SRC})

add_subdirectory(unittests)
