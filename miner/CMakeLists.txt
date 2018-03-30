add_subdirectory(pow)

set(MINER_SRC
    miner.cpp
    miner.h
)

add_library(miner STATIC ${MINER_SRC})
add_dependencies(miner pow chain pool)
target_link_libraries(miner pow chain pool)

add_subdirectory(unittests)
