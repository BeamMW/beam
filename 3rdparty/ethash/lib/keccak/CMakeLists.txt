# ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
# Copyright 2019 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

add_library(
    keccak
    keccak.c
    keccakf800.c
    keccakf1600.c
)

target_include_directories(keccak PUBLIC ${include_dir})
