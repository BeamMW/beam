set(NODE_SRC
    node.cpp
    db.cpp
    processor.cpp
    txpool.cpp
    node_client.h
    node_client.cpp
)

if(ANDROID)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-tautological-constant-out-of-range-compare")
endif()

add_library(node STATIC ${NODE_SRC})
add_dependencies(node sqlite core p2p pow)
target_link_libraries(node sqlite core p2p pow)

add_subdirectory(unittests)
add_subdirectory(laser_beam_demo)
