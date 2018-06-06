#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(P2P_SRC
    msg_reader.cpp
    fragment_writer.cpp
    msg_serializer.cpp
    common_messages.cpp
    connection.cpp
    connections.cpp
    roulette.cpp
    peer_storage.cpp
    p2p.cpp
    servers.cpp
    handshake.cpp
    ip_access_control.cpp
    rnd_gen.cpp
)

add_library(p2p STATIC ${P2P_SRC})
add_dependencies(p2p utility)
target_link_libraries(p2p utility)

add_subdirectory(unittest)
