set(NODE_SRC
    node.h
    node.cpp
)

add_library(node STATIC ${NODE_SRC})

add_executable(beam beam.cpp)

add_dependencies(beam node wallet)
target_link_libraries(beam node wallet boost_program_options)

add_subdirectory(unittests)
