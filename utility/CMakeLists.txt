set(UTILITY_SRC
    logger.cpp
    logger_checkpoints.cpp
    helpers.cpp
    config.cpp
	options.cpp
# ~etc
)

set(IO_SRC
    io/buffer.cpp
    io/bufferchain.cpp
    io/reactor.cpp
    io/asyncevent.cpp
    io/timer.cpp
    io/address.cpp
    io/tcpserver.cpp
    io/tcpstream.cpp
    io/errorhandling.cpp
    io/coarsetimer.cpp
    asynccontext.cpp
# ~etc
)

IF (MSVC)
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

if (Boost_FOUND)
    find_package(Boost COMPONENTS program_options filesystem)
    target_link_libraries(utility Boost::program_options Boost::filesystem)
else()
    target_link_libraries(utility boost_program_options boost_filesystem boost_system)
endif()

add_subdirectory(unittest)
