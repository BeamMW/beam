cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0074 NEW)

set(UTILITY_SRC
    common.cpp
    logger.cpp
    logger_checkpoints.cpp
    log_rotation.cpp
    helpers.cpp
    config.cpp
	string_helpers.cpp
	asynccontext.cpp
	fsutils.cpp
# ~etc
)

set(CLI_SRC cli/options.cpp)

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
target_include_directories(utility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(utility PUBLIC cxx_std_17)

add_library(cli STATIC ${CLI_SRC})

if (UV_INTERNAL)
    target_link_libraries(utility uv_a)
else()
    target_link_libraries(utility uv)
endif()

if (IOS)
    target_link_libraries(utility 
        $ENV{BOOST_ROOT_IOS}/lib/libboost_filesystem.a
        $ENV{BOOST_ROOT_IOS}/lib/libboost_system.a
    )
elseif(ANDROID)
    target_link_libraries(utility 
        $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_filesystem${ANDROID_SUFFIX}
        $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_system${ANDROID_SUFFIX}
    )
endif()

target_link_libraries(utility Boost::system Boost::filesystem)
target_link_libraries(cli Boost::program_options)

target_link_libraries(utility OpenSSL::SSL OpenSSL::Crypto)
if (LINUX)
    target_link_libraries(utility ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
endif()

if(OPENSSL_USE_STATIC_LIBS AND MSVC)
    target_link_libraries(utility crypt32.lib)
    if(MSVC_VERSION GREATER_EQUAL 1900)
        target_link_libraries(utility legacy_stdio_definitions.lib)
    endif()
endif()

if (UNIX)
    target_link_libraries(utility ${CMAKE_DL_LIBS})
endif()

add_subdirectory(unittest)
