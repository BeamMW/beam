set(POOL_SRC
    txpool.cpp
    txpool.h
)

add_library(pool STATIC ${POOL_SRC})

add_subdirectory(unittests)
