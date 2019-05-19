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
#include <cstdint>
#include <utility>
#include <vector>
#include <bitcoin/bitcoin/chain/point.hpp>
#include <bitcoin/bitcoin/chain/point_value.hpp>

namespace libbitcoin {
namespace chain {

// Constructors.
//-------------------------------------------------------------------------

point_value::point_value()
  : point(), value_(0)
{
}

point_value::point_value(point_value&& other)
  : value_(other.value_), point(std::move(other))
{
}

point_value::point_value(const point_value& other)
  : point(other), value_(other.value_)
{
}

point_value::point_value(point&& instance, uint64_t value)
  : point(std::move(instance)), value_(value)
{
}

point_value::point_value(const point& instance, uint64_t value)
  : point(instance), value_(value)
{
}

// Operators.
//-------------------------------------------------------------------------

// Copy and swap idiom, see: stackoverflow.com/a/3279550/1172329
point_value& point_value::operator=(point_value other)
{
    swap(*this, other);
    return *this;
}

bool point_value::operator==(const point_value& other) const
{
    return static_cast<point>(*this) == static_cast<point>(other) &&
        (value_ == other.value_);
}

bool point_value::operator!=(const point_value& other) const
{
    return !(*this == other);
}

// friend function, see: stackoverflow.com/a/5695855/1172329
void swap(point_value& left, point_value& right)
{
    using std::swap;

    // Must be unqualified (no std namespace).
    swap(static_cast<point&>(left), static_cast<point&>(right));
    swap(left.value_, right.value_);
}

// Properties (accessors).
//-------------------------------------------------------------------------

uint64_t point_value::value() const
{
    return value_;
}

void point_value::set_value(uint64_t value)
{
    value_ = value;
}

} // namespace chain
} // namespace libbitcoin
