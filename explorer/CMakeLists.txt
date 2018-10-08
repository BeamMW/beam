set(EXPLORER_SRC
    server.cpp
    adapter.cpp
)

add_library(explorer STATIC ${EXPLORER_SRC})
add_dependencies(explorer node)
target_link_libraries(explorer node)

add_subdirectory(unittest)
