// ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

#include <ethash/bit_manipulation.h>

#include <gtest/gtest.h>

TEST(bit_manipulation, rotl32)
{
    EXPECT_EQ(rotl32(0, 0), 0u);
    EXPECT_EQ(rotl32(0, 4321), 0u);

    EXPECT_EQ(rotl32(1, 0), 1u);
    EXPECT_EQ(rotl32(1, 1), 2u);
    EXPECT_EQ(rotl32(1, 2), 4u);
    EXPECT_EQ(rotl32(1, 31), 1u << 31u);
    EXPECT_EQ(rotl32(1, 32), 1u);
    EXPECT_EQ(rotl32(1, 33), 2u);

    EXPECT_EQ(rotl32(3, 0), 3u);
    EXPECT_EQ(rotl32(3, 1), 6u);
    EXPECT_EQ(rotl32(3, 30), 3u << 30u);
    EXPECT_EQ(rotl32(3, 31), (1u << 31u) | 1u);
    EXPECT_EQ(rotl32(3, 32), 3u);
    EXPECT_EQ(rotl32(3, 33), 6u);
}

TEST(bit_manipulation, rotr32)
{
    EXPECT_EQ(rotr32(0, 0), 0u);
    EXPECT_EQ(rotr32(0, 4321), 0u);

    EXPECT_EQ(rotr32(1, 0), 1u);
    EXPECT_EQ(rotr32(1, 1), 1u << 31u);
    EXPECT_EQ(rotr32(1, 2), 1u << 30u);
    EXPECT_EQ(rotr32(1, 30), 1u << 2u);
    EXPECT_EQ(rotr32(1, 31), 1u << 1u);
    EXPECT_EQ(rotr32(1, 32), 1u);
    EXPECT_EQ(rotr32(1, 33), 1u << 31u);

    EXPECT_EQ(rotr32(3, 0), 3u);
    EXPECT_EQ(rotr32(3, 1), (1u << 31u) | 1u);
    EXPECT_EQ(rotr32(3, 2), 3u << 30u);
    EXPECT_EQ(rotr32(3, 30), 12u);
    EXPECT_EQ(rotr32(3, 31), 6u);
    EXPECT_EQ(rotr32(3, 32), 3u);
    EXPECT_EQ(rotr32(3, 33), (1u << 31u) | 1u);
}

TEST(bit_manipulation, clz32)
{
    EXPECT_EQ(clz32(0), 32u);
    EXPECT_EQ(clz32(1), 31u);
    EXPECT_EQ(clz32(1u << 1u), 30u);
    EXPECT_EQ(clz32(1u << 2u), 29u);
    EXPECT_EQ(clz32(1u << 30u), 1u);
    EXPECT_EQ(clz32(1u << 31u), 0u);
    EXPECT_EQ(clz32(4321), 19u);
}

TEST(bit_manipulation, popcount32)
{
    EXPECT_EQ(popcount32(0), 0u);
    EXPECT_EQ(popcount32(1), 1u);
    EXPECT_EQ(popcount32(1u << 16u), 1u);
    EXPECT_EQ(popcount32(3), 2u);
    EXPECT_EQ(popcount32(3u << 17u), 2u);
    EXPECT_EQ(popcount32(9u << 18u), 2u);
    EXPECT_EQ(popcount32(~0u), 32u);
}

TEST(bit_manipulation, mul_hi32)
{
    EXPECT_EQ(mul_hi32(0, 0), 0u);
    EXPECT_EQ(mul_hi32(0, 1), 0u);
    EXPECT_EQ(mul_hi32(1, 0), 0u);
    EXPECT_EQ(mul_hi32(1, 1), 0u);

    EXPECT_EQ(mul_hi32(1u << 16u, 1u << 16u), 1u);
    EXPECT_EQ(mul_hi32(1u << 16u, (1u << 16u) + 1u), 1u);

    EXPECT_EQ(mul_hi32(1u << 30u, 1u << 30u), 1u << 28u);
    EXPECT_EQ(mul_hi32(1u << 31u, 1u << 31u), 1u << 30u);

    EXPECT_EQ(mul_hi32(~0u, ~0u), ~0u - 1u);
}
