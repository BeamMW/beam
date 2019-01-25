if (BEAM_TESTNET)
    set(TARGET_NAME explorer-node-testnet)
else()
    set(TARGET_NAME explorer-node)
endif()

set(EXPLORER_SRC
    server.cpp
    adapter.cpp
)

add_library(explorer STATIC ${EXPLORER_SRC})
add_dependencies(explorer node http)
target_link_libraries(explorer node http)

add_executable(${TARGET_NAME} explorer_node.cpp)
add_dependencies(${TARGET_NAME} explorer)
target_link_libraries(${TARGET_NAME} explorer)

add_subdirectory(unittest)
