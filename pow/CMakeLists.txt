set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_MODULE_PATH}")

IF (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX")
ELSE()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
ENDIF()

include_directories(impl)

set(POW_SRC
    equihash.cpp
    impl/crypto/equihash_impl.cpp
    impl/arith_uint256.cpp
    impl/uint256.cpp
    impl/utilstrencodings.cpp
)

set_source_files_properties(impl/crypto/equihash_impl.cpp PROPERTIES COMPILE_FLAGS -w)
add_library(pow STATIC ${POW_SRC})
add_dependencies(pow crypto)
target_link_libraries(pow crypto)
target_include_directories(pow PRIVATE ${PROJECT_SOURCE_DIR}/utility/crypto)
target_compile_definitions(pow PRIVATE ENABLE_MINING)

add_subdirectory(unittests)
