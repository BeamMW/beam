cmake_minimum_required(VERSION 3.1)
project(beam)

enable_testing()

function(add_test_snippet EXE_NAME LIB_NAME)
    add_executable(${EXE_NAME} ${EXE_NAME}.cpp)
    add_dependencies(${EXE_NAME} ${LIB_NAME})
    target_link_libraries(${EXE_NAME} ${LIB_NAME})
    add_test( ${EXE_NAME} ${EXE_NAME} )
endfunction()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall -pthread")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -ggdb3")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

if(UNIX)
    set(UV_INTERNAL ON)
    add_definitions(-DUV_INTERNAL)
endif()

if (WIN32)
    set(Boost_DEBUG On)
    find_package(Boost)
    if (Boost_FOUND)
        include_directories(${Boost_INCLUDE_DIRS})
        add_definitions( -DBOOST_ALL_DYN_LINK )
    endif()
endif()

include_directories(${PROJECT_SOURCE_DIR})
add_definitions(-DPROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include_directories(${PROJECT_SOURCE_DIR}/utility) # TODO  move to ./3rdparty all that stuff that requires to add this line

add_subdirectory(utility)
add_subdirectory(core)
add_subdirectory(miner)
add_subdirectory(chain)
add_subdirectory(pool)
add_subdirectory(p2p)
add_subdirectory(wallet)
add_subdirectory(beam)
