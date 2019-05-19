/**
 * Copyright (c) 2011-2017 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LIBBITCOIN_BINARY_HPP
#define LIBBITCOIN_BINARY_HPP

#include <cstdint>
#include <string>
#include <bitcoin/bitcoin/constants.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>

namespace libbitcoin {

class BC_API binary
{
public:
    typedef uint8_t block;
    typedef std::size_t size_type;

    static BC_CONSTEXPR size_type bits_per_block = byte_bits;
    static size_type blocks_size(size_type bit_size);
    static bool is_base2(const std::string& text);

    binary();
    binary(const binary& other);
    binary(const std::string& bit_string);
    binary(size_type size, uint32_t number);
    binary(size_type size, data_slice blocks);

    void resize(size_type size);
    bool operator[](size_type index) const;
    const data_chunk& blocks() const;
    std::string encoded() const;

    /// size in bits
    size_type size() const;
    void append(const binary& post);
    void prepend(const binary& prior);
    void shift_left(size_type distance);
    void shift_right(size_type distance);
    binary substring(size_type first, size_type length=max_size_t) const;

    bool is_prefix_of(data_slice field) const;
    bool is_prefix_of(uint32_t field) const;
    bool is_prefix_of(const binary& field) const;

    bool operator<(const binary& other) const;
    bool operator==(const binary& other) const;
    bool operator!=(const binary& other) const;
    binary& operator=(const binary& other);
    friend std::istream& operator>>(std::istream& in, binary& to);
    friend std::ostream& operator<<(std::ostream& out, const binary& of);

private:
    static uint8_t shift_block_right(uint8_t next, uint8_t current, uint8_t prior,
        size_type original_offset, size_type intended_offset);

    data_chunk blocks_;
    uint8_t final_block_excess_;
};

} // namespace libbitcoin

namespace std
{

template<>
struct hash<bc::binary>
{
    size_t operator()(const bc::binary& value) const
    {
        return std::hash<std::string>()(value.encoded());
    }
};

} // namespace std

#endif
