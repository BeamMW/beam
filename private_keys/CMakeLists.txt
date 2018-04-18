cmake_minimum_required(VERSION 3.4)
project(TestLib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_CURRENT_BINARY_DIR}/lib)


include_directories(./include
                    ../core)


add_executable(main ./src/main.cpp
                    ./src/storage.cpp
                    ./src/private_key.cpp
                    )

target_link_libraries(main core)
