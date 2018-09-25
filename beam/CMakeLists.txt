set(TARGET_NAME beam-node)

set(NODE_SRC
    node.cpp
    node_db.cpp
    node_processor.cpp
)

add_library(node STATIC ${NODE_SRC})
add_dependencies(node sqlite core p2p pow)
target_link_libraries(node sqlite core p2p pow)

add_executable(${TARGET_NAME} cli.cpp)

if(LINUX)
	target_link_libraries(${TARGET_NAME} -static-libstdc++ -static-libgcc)
endif()

configure_file("${CMAKE_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_dependencies(${TARGET_NAME} node utility)
target_link_libraries(${TARGET_NAME} node utility)

add_subdirectory(unittests)
add_subdirectory(functionaltests)
