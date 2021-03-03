cmake_minimum_required(VERSION 3.13)

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
        bvm
    PRIVATE
        sqlite
)
if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittests)
endif()
if (NOT BEAM_WALLET_CLIENT_LIBRARY)
    add_subdirectory(utils)
endif()
