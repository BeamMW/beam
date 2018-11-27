set(EXPLORER_SRC
    server.cpp
    adapter.cpp
)

add_library(explorer STATIC ${EXPLORER_SRC})
add_dependencies(explorer node http)
target_link_libraries(explorer node http)

add_executable(explorer-node explorer_node.cpp)
add_dependencies(explorer-node explorer)
target_link_libraries(explorer-node explorer)

add_subdirectory(unittest)
