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
#include <bitcoin/bitcoin/config/hash160.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <boost/program_options.hpp>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/formats/base_16.hpp>
#include <bitcoin/bitcoin/math/hash.hpp>

namespace libbitcoin {
namespace config {

hash160::hash160()
  : value_(null_short_hash)
{
}

hash160::hash160(const std::string& hexcode)
{
    std::stringstream(hexcode) >> *this;
}

hash160::hash160(const short_hash& value)
  : value_(value)
{
}

hash160::hash160(const hash160& other)
  : hash160(other.value_)
{
}

hash160::operator const short_hash&() const
{
    return value_;
}

std::istream& operator>>(std::istream& input, hash160& argument)
{
    std::string hexcode;
    input >> hexcode;

    if (!decode_base16(argument.value_, hexcode))
    {
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(hexcode));
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, const hash160& argument)
{
    output << encode_base16(argument.value_);
    return output;
}

} // namespace config
} // namespace libbitcoin
