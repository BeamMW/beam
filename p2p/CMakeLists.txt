cmake_minimum_required(VERSION 3.13)

set(P2P_SRC
    msg_reader.cpp
    msg_serializer.cpp
    protocol_base.cpp
    line_protocol.h)

add_library(p2p STATIC ${P2P_SRC})
target_link_libraries(p2p PUBLIC utility)

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittest)
endif()
