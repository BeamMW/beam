add_executable(hw_crypto_test hw_crypto_test.cpp)
target_link_libraries(hw_crypto_test hw_crypto core wallet ${ARGN})
add_test(NAME hw_crypto_test COMMAND $<TARGET_FILE:hw_crypto_test>)
