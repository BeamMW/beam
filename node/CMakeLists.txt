set(NODE_SRC
    node.cpp
    node.h
)

add_library(node STATIC ${NODE_SRC})
add_dependencies(node sqlite)
target_link_libraries(node sqlite)
