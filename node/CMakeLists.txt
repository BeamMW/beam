cmake_minimum_required(VERSION 3.11)

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

target_link_libraries(node
    PUBLIC
        core
    PRIVATE
        sqlite
)

add_subdirectory(unittests)
add_subdirectory(laser_beam_demo)
