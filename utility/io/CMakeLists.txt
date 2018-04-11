set(IO_SRC
    bufferchain.cpp
    reactor.cpp
    asyncevent.cpp
    timer.cpp
    address.cpp
    tcpserver.cpp
    tcpstream.cpp
# ~etc
)

if(UV_INTERNAL)
    include(libuv.cmake)
endif()

add_library(io STATIC ${IO_SRC})
if (UV_INTERNAL)
    add_dependencies(io uvinternal)
    target_link_libraries(io uvinternal)
else()
    target_link_libraries(io uv)
endif()


add_subdirectory(unittest)
