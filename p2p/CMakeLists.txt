set(P2P_SRC
    msg_reader.cpp
    msg_serializer.cpp
    protocol_base.cpp
    line_protocol.h)

add_library(p2p STATIC ${P2P_SRC})
add_dependencies(p2p utility)
target_link_libraries(p2p utility)

add_subdirectory(unittest)
