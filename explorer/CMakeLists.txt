set(EXPLORER_SRC
    server.cpp
    adapter.cpp
)

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")

add_library(explorer STATIC ${EXPLORER_SRC})

add_dependencies(explorer node http)
target_link_libraries(explorer node http)

add_executable(explorer-node explorer_node.cpp)
target_include_directories(explorer-node PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(explorer-node explorer)
target_link_libraries(explorer-node explorer)

add_subdirectory(unittest)
