set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_MODULE_PATH}")

include_directories(impl)

set(POW_SRC
    equihash.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/crypto/equihash_impl.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/arith_uint256.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/uint256.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/utilstrencodings.cpp
)

if(UNIX)
    set_source_files_properties(impl/crypto/equihash_impl.cpp PROPERTIES COMPILE_FLAGS -O2)
endif()

add_library(pow STATIC ${POW_SRC})

target_compile_definitions(pow PRIVATE ENABLE_MINING)

add_dependencies(pow crypto)
target_link_libraries(pow crypto)
if(Boost_FOUND)
    target_link_libraries(pow boost)
endif()

target_include_directories(pow PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/crypto)

add_library(external_pow STATIC
    external_pow_stub.cpp
    stratum.cpp
    stratum_server.cpp
)

add_dependencies(external_pow core p2p pow)
target_link_libraries(external_pow p2p pow core)

add_executable(miner_client miner_client.cpp ../core/block_crypt.cpp) # ???????????????????????????

add_dependencies(miner_client external_pow)
target_link_libraries(miner_client external_pow)

if(ANDROID)
    if(ANDROID_ABI STREQUAL "armeabi-v7a")
        target_link_libraries(miner_client 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_program_options-clang-mt-a32-1_68.a)
    elseif(ANDROID_ABI STREQUAL "x86")
        target_link_libraries(miner_client 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_program_options-clang-mt-x32-1_68.a)
    elseif(ANDROID_ABI STREQUAL "x86_64")
        target_link_libraries(miner_client 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_program_options-clang-mt-x64-1_68.a)
    elseif(ANDROID_ABI STREQUAL "arm64-v8a")
        target_link_libraries(miner_client 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_program_options-clang-mt-a64-1_68.a)
    endif()
else()
    if (Boost_FOUND)
        find_package(Boost COMPONENTS program_options)
        add_dependencies(miner_client Boost::program_options)
        target_link_libraries(miner_client Boost::program_options)
    else()
        add_dependencies(miner_client boost_program_options)
        target_link_libraries(miner_client boost_program_options)
    endif()
endif()
add_subdirectory(unittests)
