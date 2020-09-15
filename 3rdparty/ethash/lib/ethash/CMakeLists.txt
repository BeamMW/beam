# ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
# Copyright 2018-2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

add_library(
    ethash
    bit_manipulation.h
    builtins.h
    endianness.hpp
    ethash-internal.hpp
    ethash.cpp
    managed.cpp
    kiss99.hpp
    primes.h
    primes.c
    progpow.cpp
)

target_link_libraries(ethash PRIVATE keccak)
target_include_directories(ethash PUBLIC ${include_dir})
