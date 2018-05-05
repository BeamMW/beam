set(UTILITY_SRC
    logger.cpp
    logger_checkpoints.cpp
    helpers.cpp
    config.cpp
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
    io/exception.cpp
# ~etc
)

IF (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX")
ELSE()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
ENDIF()

add_library(utility STATIC ${UTILITY_SRC} ${IO_SRC})
add_library(crypto STATIC crypto/blake/blake2b.cpp)
include_directories(${PROJECT_SOURCE_DIR}/utility)

if (UV_INTERNAL)
    include(io/libuv.cmake)
    add_dependencies(utility uvinternal)
    target_link_libraries(utility uvinternal)
else()
    target_link_libraries(utility uv)
endif()

add_subdirectory(unittest)
