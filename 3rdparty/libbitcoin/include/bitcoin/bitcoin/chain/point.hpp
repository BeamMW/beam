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
#ifndef LIBBITCOIN_CHAIN_POINT_HPP
#define LIBBITCOIN_CHAIN_POINT_HPP

#include <cstdint>
#include <istream>
#include <string>
#include <vector>
#include <boost/functional/hash.hpp>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/chain/point_iterator.hpp>
#include <bitcoin/bitcoin/math/hash.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>
#include <bitcoin/bitcoin/utility/reader.hpp>
#include <bitcoin/bitcoin/utility/writer.hpp>

namespace libbitcoin {
namespace chain {

class BC_API point
{
public:
    /// This is a sentinel used in .index to indicate no output, e.g. coinbase.
    /// This value is serialized and defined by consensus, not implementation.
    static const uint32_t null_index;

    typedef std::vector<point> list;
    typedef std::vector<uint32_t> indexes;

    // Constructors.
    //-------------------------------------------------------------------------

    point();

    point(point&& other);
    point(const point& other);

    point(hash_digest&& hash, uint32_t index);
    point(const hash_digest& hash, uint32_t index);

    // Operators.
    //-------------------------------------------------------------------------

    /// This class is move assignable and copy assignable.
    point& operator=(point&& other);
    point& operator=(const point& other);

    bool operator<(const point& other) const;
    bool operator==(const point& other) const;
    bool operator!=(const point& other) const;

    // Deserialization.
    //-------------------------------------------------------------------------

    static point factory_from_data(const data_chunk& data, bool wire=true);
    static point factory_from_data(std::istream& stream, bool wire=true);
    static point factory_from_data(reader& source, bool wire=true);

    bool from_data(const data_chunk& data, bool wire=true);
    bool from_data(std::istream& stream, bool wire=true);
    bool from_data(reader& source, bool wire=true);

    bool is_valid() const;

    // Serialization.
    //-------------------------------------------------------------------------

    data_chunk to_data(bool wire=true) const;
    void to_data(std::ostream& stream, bool wire=true) const;
    void to_data(writer& sink, bool wire=true) const;

    // Iteration (limited to store serialization).
    //-------------------------------------------------------------------------

    point_iterator begin() const;
    point_iterator end() const;

    // Properties (size, accessors, cache).
    //-------------------------------------------------------------------------

    static size_t satoshi_fixed_size();
    size_t serialized_size(bool wire=true) const;

    // deprecated (unsafe)
    hash_digest& hash();

    const hash_digest& hash() const;
    void set_hash(hash_digest&& value);
    void set_hash(const hash_digest& value);

    uint32_t index() const;
    void set_index(uint32_t value);

    // Utilities.
    //-------------------------------------------------------------------------

    /// This is for client-server, not related to consensus or p2p networking.
    uint64_t checksum() const;

    // Validation.
    //-------------------------------------------------------------------------

    bool is_null() const;

protected:
    point(hash_digest&& hash, uint32_t index, bool valid);
    point(const hash_digest& hash, uint32_t index, bool valid);
    void reset();

private:
    hash_digest hash_;
    uint32_t index_;
    bool valid_;
};

} // namespace chain
} // namespace libbitcoin


// Standard hash.
//-----------------------------------------------------------------------------

namespace std
{

// Extend std namespace with our hash wrapper (database key, not checksum).
template <>
struct hash<bc::chain::point>
{
    size_t operator()(const bc::chain::point& point) const
    {
        size_t seed = 0;
        boost::hash_combine(seed, point.hash());
        boost::hash_combine(seed, point.index());
        return seed;
    }
};

// Extend std namespace with the non-wire size of point (database key size).
template <>
struct tuple_size<bc::chain::point>
{
    static const auto value = std::tuple_size<bc::hash_digest>::value +
        sizeof(uint16_t);

    operator std::size_t() const
    {
        return value;
    }
};

} // namespace std

#endif
