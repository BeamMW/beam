cmake_minimum_required(VERSION 3.11)

set(LASER_SRC
    channel.cpp
    connection.cpp
    mediator.cpp
    receiver.cpp
)

add_library(laser STATIC ${LASER_SRC})
target_link_libraries(laser PUBLIC wallet_core)
target_compile_definitions(laser PUBLIC BEAM_LASER_SUPPORT)
