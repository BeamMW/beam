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

if(BEAM_USE_GPU)

    find_package(OpenCL REQUIRED)

    if(OpenCL_FOUND)
        include_directories(${OpenCL_INCLUDE_DIRS})
        link_directories(${OpenCL_LIBRARIES})
        message(STATUS "Using OpenCL ${OpenCL_VERSION_STRING}")
    else()
        message(FATAL_ERROR "error, OpenCL not found")
    endif()

    include(bin2h.cmake)

    bin2h(SOURCE_FILE ${PROJECT_SOURCE_DIR}/3rdparty/opencl-miner/equihash_150_5.cl HEADER_FILE ${PROJECT_SOURCE_DIR}/3rdparty/opencl-miner/equihash_150_5.dat)

    set(POW_SRC ${POW_SRC}
        ${PROJECT_SOURCE_DIR}/3rdparty/equihash_gpu.cpp
        ${PROJECT_SOURCE_DIR}/3rdparty/opencl-miner/clHost.cpp)

    add_library(pow STATIC ${POW_SRC})

    target_include_directories(pow PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/opencl-miner)
    target_include_directories(pow PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/opencl-miner/clHeaders)

    target_link_libraries(pow OpenCL::OpenCL)
else()
    add_library(pow STATIC ${POW_SRC})
endif()

target_compile_definitions(pow PRIVATE ENABLE_MINING)

add_dependencies(pow crypto)
target_link_libraries(pow crypto)

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

if(BEAM_USE_GPU)
    add_dependencies(miner_client gpu_tools)
    target_link_libraries(miner_client gpu_tools)
endif()

add_subdirectory(unittests)
