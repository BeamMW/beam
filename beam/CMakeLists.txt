set(NODE_SRC
    node.cpp
    node_db.cpp
)

add_library(node STATIC ${NODE_SRC})
add_dependencies(node chain pool sqlite)
target_link_libraries(node chain pool sqlite)

add_executable(beam beam.cpp)

add_dependencies(beam node wallet)
target_link_libraries(beam node wallet boost_program_options)

add_subdirectory(unittests)
