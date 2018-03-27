include_directories(impl)

set(POW_SRC
    equihash.cpp
    equihash.h
    impl/crypto/equihash.cpp
    impl/arith_uint256.cpp
    impl/uint256.cpp
    impl/utilstrencodings.cpp
)

add_library(pow STATIC ${POW_SRC})
target_compile_definitions(pow PRIVATE ENABLE_MINING)
target_link_libraries(pow -lsodium)
add_subdirectory(unittests)
