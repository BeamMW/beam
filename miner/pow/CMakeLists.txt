set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_MODULE_PATH}")

IF (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX")
ELSE()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
    
ENDIF()

include_directories(impl)

set(POW_SRC
    equihash.cpp
    equihash.h
    impl/crypto/equihash_impl.cpp
    impl/crypto/blake/blake2b.cpp
    impl/arith_uint256.cpp
    impl/uint256.cpp
    impl/utilstrencodings.cpp
)

add_library(pow STATIC ${POW_SRC})
target_compile_definitions(pow PRIVATE ENABLE_MINING)
