cmake_minimum_required(VERSION 3.13)

add_test_snippet(equihash_test pow)
target_link_libraries(equihash_test pow core)

add_test_snippet(stratum_test external_pow)

add_executable(server_stub server_stub.cpp ../../core/block_crypt.cpp) # ???????????????????????????
target_link_libraries(server_stub external_pow node)
