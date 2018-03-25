set(IO_SRC
    reactor.cpp

# ~etc
)

include(libuv.cmake)

add_library(io STATIC ${IO_SRC})
add_dependencies(io uv)
target_link_libraries(io uv)

add_subdirectory(unittest)
