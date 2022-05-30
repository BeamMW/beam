cmake_minimum_required(VERSION 3.15)

add_library(shader_debugger STATIC)

target_sources(shader_debugger 
    PRIVATE
        shaders_debugger.cpp
    PUBLIC
        shader_debugger.h
)

target_link_libraries(shader_debugger
    PUBLIC
        dwarf
        node
        Boost::boost
)

add_library(dap_client STATIC)

target_sources(dap_client 
    PRIVATE
        dap_client.cpp
    PUBLIC
        dap_client.h
)

target_link_libraries(dap_client
    PUBLIC
        cppdap
        shader_debugger
)