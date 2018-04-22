cmake_minimum_required(VERSION 3.4)
project(TestLib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_CURRENT_BINARY_DIR}/lib)


include_directories(./include
                    ../core)


add_executable(testkey ./src/main.cpp
					   ./src/utill.cpp
                       ./src/utxo.cpp
                       ./src/private_key.cpp)

target_link_libraries(testkey core)
