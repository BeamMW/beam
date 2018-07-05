set(NODE_SRC
    node.cpp
    node_db.cpp
    node_processor.cpp
)

add_library(node STATIC ${NODE_SRC})
add_dependencies(node sqlite core p2p pow)
target_link_libraries(node sqlite core p2p pow)

add_executable(beam beam.cpp)

add_dependencies(beam node wallet)

if (Boost_FOUND)
	find_package(Boost COMPONENTS program_options)
    target_link_libraries(beam Boost::program_options)
else()
    target_link_libraries(beam boost_program_options)
endif()

target_link_libraries(beam node wallet)

add_subdirectory(unittests)
add_subdirectory(functionaltests)
