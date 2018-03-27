set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_MODULE_PATH}")

find_package(sodium REQUIRED)

include_directories(impl)

set(POW_SRC
    equihash.cpp
    equihash.h
    impl/crypto/equihash_impl.cpp
    impl/arith_uint256.cpp
    impl/uint256.cpp
    impl/utilstrencodings.cpp
)

add_library(pow STATIC ${POW_SRC})
target_compile_definitions(pow PRIVATE ENABLE_MINING)
target_link_libraries(pow sodium)

add_subdirectory(unittests)
