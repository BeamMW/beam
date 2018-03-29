add_subdirectory(pow)

set(MINER_SRC
    miner.cpp
    miner.h
)

add_library(miner STATIC ${MINER_SRC})
add_dependencies(miner pow)
target_link_libraries(miner pow)

add_subdirectory(unittests)
