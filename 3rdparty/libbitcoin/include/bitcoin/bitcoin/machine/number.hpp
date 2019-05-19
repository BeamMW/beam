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
#ifndef LIBBITCOIN_MACHINE_NUMBER_HPP
#define LIBBITCOIN_MACHINE_NUMBER_HPP

#include <cstddef>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>

namespace libbitcoin {
namespace machine {

/**
 * Numeric opcodes (OP_1ADD, etc) are restricted to operating on
 * 4-byte integers. The semantics are subtle, though: operands must be
 * in the range [-2^31 +1...2^31 -1], but results may overflow (and are
 * valid as long as they are not used in a subsequent numeric operation).
 *
 * number enforces those semantics by storing results as
 * an int64 and allowing out-of-range values to be returned as a vector of
 * bytes but throwing an exception if arithmetic is done or the result is
 * interpreted as an integer.
 */
class BC_API number
{
public:
    static const uint8_t negative_1;
    static const uint8_t negative_0;
    static const uint8_t positive_0;
    static const uint8_t positive_1;
    static const uint8_t positive_2;
    static const uint8_t positive_3;
    static const uint8_t positive_4;
    static const uint8_t positive_5;
    static const uint8_t positive_6;
    static const uint8_t positive_7;
    static const uint8_t positive_8;
    static const uint8_t positive_9;
    static const uint8_t positive_10;
    static const uint8_t positive_11;
    static const uint8_t positive_12;
    static const uint8_t positive_13;
    static const uint8_t positive_14;
    static const uint8_t positive_15;
    static const uint8_t positive_16;
    static const uint8_t negative_mask;

    /// Construct with zero value.
    number();

    /// Construct with specified value.
    explicit number(int64_t value);

    /// Replace the value derived from a byte vector with LSB first ordering.
    bool set_data(const data_chunk& data, size_t max_size);

    // Properties
    //-------------------------------------------------------------------------

    /// Return the value as a byte vector with LSB first ordering.
    data_chunk data() const;

    /// Return the value bounded by the limits of int32.
    int32_t int32() const;

    /// Return the unbounded value.
    int64_t int64() const;

    // Stack Helpers
    //-------------------------------------------------------------------------

    /// Return value as stack boolean (nonzero is true).
    bool is_true() const;

    /// Return value as stack boolean (zero is false).
    bool is_false() const;

    // Operators
    //-------------------------------------------------------------------------

    //*************************************************************************
    // CONSENSUS: script::number implements consensus critical overflow
    // behavior for all operators, specifically [-, +, +=, -=].
    //*************************************************************************

    bool operator>(int64_t value) const;
    bool operator<(int64_t value) const;
    bool operator>=(int64_t value) const;
    bool operator<=(int64_t value) const;
    bool operator==(int64_t value) const;
    bool operator!=(int64_t value) const;

    bool operator>(const number& other) const;
    bool operator<(const number& other) const;
    bool operator>=(const number& other) const;
    bool operator<=(const number& other) const;
    bool operator==(const number& other) const;
    bool operator!=(const number& other) const;

    number operator+() const;
    number operator-() const;
    number operator+(int64_t value) const;
    number operator-(int64_t value) const;
    number operator+(const number& other) const;
    number operator-(const number& other) const;

    number& operator+=(int64_t value);
    number& operator-=(int64_t value);
    number& operator+=(const number& other);
    number& operator-=(const number& other);

private:
    int64_t value_;
};

} // namespace machine
} // namespace libbitcoin


#include <bitcoin/bitcoin/impl/machine/number.ipp>

#endif
