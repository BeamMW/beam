cmake_minimum_required(VERSION 3.13)

if(LINUX)
	target_link_libraries(laser_beam_demo -static-libstdc++ -static-libgcc)
endif()

add_executable(laser_beam_demo laser_beam_demo.cpp)
target_link_libraries(laser_beam_demo node)
