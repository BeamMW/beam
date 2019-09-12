cmake_minimum_required(VERSION 3.11)

if(ANDROID OR NOT BEAM_USE_AVX)
    add_library(blake2b STATIC ref/blake2b-ref.c)
else()
    add_library(blake2b STATIC sse/blake2b.cpp)
endif()

target_include_directories(blake2b PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})