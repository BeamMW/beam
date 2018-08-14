set(NODE_SRC
    node.cpp
    node_db.cpp
    node_processor.cpp
)

add_library(node STATIC ${NODE_SRC})
add_dependencies(node sqlite core p2p pow)
target_link_libraries(node sqlite core p2p pow)

add_executable(beam beam.cpp)

configure_file("${CMAKE_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(beam PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_dependencies(beam node wallet)

target_link_libraries(beam node wallet)

add_subdirectory(unittests)
add_subdirectory(functionaltests)
