add_subdirectory(pow)

set(MINER_SRC
    miner.cpp
    miner.h
)

add_library(miner STATIC ${MINER_SRC})
add_dependencies(miner pow chain pool crypto)
target_include_directories(miner PRIVATE ${PROJECT_SOURCE_DIR}/utility/crypto)
target_link_libraries(miner pow chain pool crypto)

add_subdirectory(unittests)
