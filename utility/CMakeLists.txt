set(UTILITY_SRC
    logger.cpp
    logger_checkpoints.cpp
    helpers.cpp
    config.cpp
	options.cpp
	string_helpers.cpp
	asynccontext.cpp
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
    #io/sslserver.cpp
    io/tcpstream.cpp
    #io/sslstream.cpp
    io/errorhandling.cpp
    io/coarsetimer.cpp
    io/fragment_writer.cpp
# ~etc
)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX")
    set_source_files_properties(logger.cpp PROPERTIES COMPILE_FLAGS "/wd4996") # 'std::fpos<_Mbstatet>::seekpos': warning STL4019: The member std::fpos::seekpos() is non-Standard
elseif(ANDROID)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
endif()

add_library(utility STATIC ${UTILITY_SRC} ${IO_SRC})

if(ANDROID)
    add_library(crypto STATIC crypto/blake/ref/blake2b-ref.c)
else()
    add_library(crypto STATIC crypto/blake/sse/blake2b.cpp)
endif()

include_directories(${PROJECT_SOURCE_DIR}/utility)

if (UV_INTERNAL)
    include(io/libuv.cmake)
    add_dependencies(utility uvinternal)
    target_link_libraries(utility uvinternal)
else()
    target_link_libraries(utility uv)
endif()

if(ANDROID)
    target_link_libraries(utility
        $ENV{BOOST_ROOT_ANDROID}/lib/libboost_program_options-clang-mt-a32-1_68.a
        $ENV{BOOST_ROOT_ANDROID}/lib/libboost_filesystem-clang-mt-a32-1_68.a
        $ENV{BOOST_ROOT_ANDROID}/lib/libboost_system-clang-mt-a32-1_68.a)

else()
    if (Boost_FOUND)
        find_package(Boost COMPONENTS program_options filesystem)
        target_link_libraries(utility Boost::program_options Boost::filesystem)
    else()
        target_link_libraries(utility boost_program_options boost_filesystem boost_system)
    endif()
endif()

add_subdirectory(unittest)
