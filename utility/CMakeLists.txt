set(UTILITY_SRC
    logger.cpp
    logger_checkpoints.cpp
    helpers.cpp
# ~etc
)

set(IO_SRC
    io/bufferchain.cpp
    io/reactor.cpp
    io/asyncevent.cpp
    io/timer.cpp
    io/address.cpp
    io/tcpserver.cpp
    io/tcpstream.cpp
# ~etc
)


add_library(utility STATIC ${UTILITY_SRC} ${IO_SRC})

if (UV_INTERNAL)
    include(io/libuv.cmake)
    add_dependencies(utility uvinternal)
    target_link_libraries(utility uvinternal)
else()
    target_link_libraries(utility uv)
endif()

add_subdirectory(unittest)
