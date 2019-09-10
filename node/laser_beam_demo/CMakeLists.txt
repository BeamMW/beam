cmake_minimum_required(VERSION 3.11)

set(TARGET_NAME laser_beam_demo)

add_executable(${TARGET_NAME} laser_beam_demo.cpp)

if(LINUX)
	target_link_libraries(${TARGET_NAME} -static-libstdc++ -static-libgcc)
endif()

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_dependencies(${TARGET_NAME} node)
target_link_libraries(${TARGET_NAME} node)
