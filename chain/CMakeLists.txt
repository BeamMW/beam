
set(CHAIN_SRC
    chain.cpp
    chain.h
)

add_library(chain STATIC ${CHAIN_SRC})

add_subdirectory(unittests)
