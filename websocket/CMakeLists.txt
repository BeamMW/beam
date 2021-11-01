cmake_minimum_required(VERSION 3.13)

#set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_library(websocket STATIC
    websocket_server.h 
    websocket_server.cpp 
    reactor.h 
    reactor.cpp 
    sessions.h 
    sessions.cpp
    )

target_link_libraries(websocket
    PUBLIC
        Boost::boost
    PRIVATE
        utility
        beam
)

target_compile_definitions(websocket PUBLIC _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)

if(MSVC)
    # to avoid 4702 warning here, until another workaround will be found
    # const_buffer(const mutable_buffer& b) BOOST_ASIO_NOEXCEPT
    #    : data_(b.data()),
    target_compile_options(websocket PRIVATE "/wd4702")
    target_compile_options(websocket PRIVATE "/bigobj")
endif()

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittests)
endif()



