cmake_minimum_required(VERSION 3.13)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_MODULE_PATH}")

include_directories(impl)

set(POW_SRC
    beamHash.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/crypto/equihashR_impl.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/crypto/beamHashIII_impl.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/arith_uint256.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/uint256.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/utilstrencodings.cpp
)

if(UNIX)
    set_source_files_properties(impl/crypto/equihashR_impl.cpp PROPERTIES COMPILE_FLAGS -O2)
endif()

add_library(pow STATIC ${POW_SRC})

target_compile_definitions(pow PUBLIC ENABLE_MINING)

target_link_libraries(pow 
    PRIVATE
        Boost::boost
        blake2b
        core
)

if(NOT BEAM_BVM_ONLY)
    add_library(external_pow STATIC
        external_pow_stub.cpp
        stratum.cpp
        stratum_server.cpp
    )

    target_link_libraries(external_pow p2p pow core)
endif()

if (NOT BEAM_WALLET_CLIENT_LIBRARY)
    add_executable(miner_client miner_client.cpp ../core/block_crypt.cpp) # ???????????????????????????

    target_link_libraries(miner_client external_pow Boost::program_options)
endif()

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittests)
endif()
