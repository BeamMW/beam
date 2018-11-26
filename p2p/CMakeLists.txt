set(P2P_SRC
    msg_reader.cpp
    msg_serializer.cpp
    protocol_base.cpp
    http_msg_reader.cpp
    http_msg_creator.cpp
    http_client.cpp
    line_protocol.h
    json_serializer.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/picohttpparser/picohttpparser.c)

add_library(p2p STATIC ${P2P_SRC})
add_dependencies(p2p utility)
target_link_libraries(p2p utility)

add_subdirectory(unittest)
