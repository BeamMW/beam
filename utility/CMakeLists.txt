set(UTILITY_SRC
    common.cpp
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
    io/sslserver.cpp
    io/sslio.cpp
    io/tcpstream.cpp
    io/sslstream.cpp
    io/errorhandling.cpp
    io/coarsetimer.cpp
    io/fragment_writer.cpp
    io/json_serializer.cpp
# ~etc
)

if (BEAM_USE_AVX)   
    if (MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    elseif(ANDROID)
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse3")
    endif()
endif()


if (MSVC)
    set_source_files_properties(logger.cpp PROPERTIES COMPILE_FLAGS "/wd4996") # 'std::fpos<_Mbstatet>::seekpos': warning STL4019: The member std::fpos::seekpos() is non-Standard
endif()

add_library(utility STATIC ${UTILITY_SRC} ${IO_SRC})

if(ANDROID OR NOT BEAM_USE_AVX)
    add_library(crypto STATIC ${PROJECT_SOURCE_DIR}/3rdparty/crypto/blake/ref/blake2b-ref.c)
else()
    add_library(crypto STATIC ${PROJECT_SOURCE_DIR}/3rdparty/crypto/blake/sse/blake2b.cpp)
endif()

if (UV_INTERNAL)
    include(io/libuv.cmake)
    add_dependencies(utility uvinternal)
    target_link_libraries(utility uvinternal)
else()
    target_link_libraries(utility uv)
endif()

target_link_libraries(utility ${OPENSSL_LIBSSL} ${OPENSSL_LIBCRYPTO})

if (UNIX)
    target_link_libraries(utility dl)
endif()

target_link_libraries(utility mnemonic)

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

if(BEAM_USE_GPU)
    add_subdirectory(gpu)
endif()

add_subdirectory(unittest)
