cmake_minimum_required(VERSION 3.11)
cmake_policy(SET CMP0074 NEW)

find_package(OpenSSL REQUIRED)
find_package(Boost 1.67 REQUIRED COMPONENTS program_options filesystem)

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
target_compile_features(utility PUBLIC cxx_std_17)


#if(NOT ANDROID)
    add_library(cli STATIC ${CLI_SRC})
    target_link_libraries(cli Boost::program_options)
#endif()

if (UV_INTERNAL)
    target_link_libraries(utility uv_a)
else()
    target_link_libraries(utility uv)
endif()

target_link_libraries(utility OpenSSL::SSL OpenSSL::Crypto)
if(OPENSSL_USE_STATIC_LIBS AND MSVC)
    target_link_libraries(utility crypt32.lib)
    if(MSVC_VERSION GREATER_EQUAL 1900)
        target_link_libraries(utility legacy_stdio_definitions.lib)
    endif()
endif()

if (UNIX)
    target_link_libraries(utility dl)
endif()

if (IOS)
    target_link_libraries(utility 
	$ENV{BOOST_ROOT_IOS}/lib/libboost_filesystem.a
	$ENV{BOOST_ROOT_IOS}/lib/libboost_system.a)
elseif(ANDROID)
    if(ANDROID_ABI STREQUAL "armeabi-v7a")
        target_link_libraries(utility 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_filesystem-clang-mt-a32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_system-clang-mt-a32-1_68.a)
    elseif(ANDROID_ABI STREQUAL "x86")
        target_link_libraries(utility 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_filesystem-clang-mt-x32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_system-clang-mt-x32-1_68.a)
    elseif(ANDROID_ABI STREQUAL "x86_64")
        target_link_libraries(utility 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_filesystem-clang-mt-x64-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_system-clang-mt-x64-1_68.a)
    elseif(ANDROID_ABI STREQUAL "arm64-v8a")
        target_link_libraries(utility 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_filesystem-clang-mt-a64-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_system-clang-mt-a64-1_68.a)
    endif()
else()
    target_link_libraries(utility Boost::filesystem)
endif()

add_subdirectory(unittest)
