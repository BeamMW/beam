set(P2P_SRC
    msg_reader.cpp
    msg_serializer.cpp
    protocol_base.cpp
    connected_peers.cpp
    roulette.cpp
    p2p.cpp
    handshake.cpp
    rnd_gen.cpp
    connect_pool.cpp
    http_msg_reader.cpp
    http_msg_creator.cpp
    http_client.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/picohttpparser/picohttpparser.c)

add_library(p2p STATIC ${P2P_SRC})
add_dependencies(p2p utility)
target_link_libraries(p2p utility)

add_subdirectory(unittest)
