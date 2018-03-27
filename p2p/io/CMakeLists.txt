set(IO_SRC
    reactor.cpp
    asyncevent.cpp
    timer.cpp
    address.cpp
    tcpserver.cpp
    tcpstream.cpp
    tcpconnector.cpp
# ~etc
)

include(libuv.cmake)

add_library(io STATIC ${IO_SRC})
add_dependencies(io uv)
target_link_libraries(io uv)

add_subdirectory(unittest)
