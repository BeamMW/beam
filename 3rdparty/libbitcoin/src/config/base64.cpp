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
#include <bitcoin/bitcoin/config/base64.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <boost/program_options.hpp>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/formats/base_64.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>

namespace libbitcoin {
namespace config {

base64::base64()
{
}

base64::base64(const std::string& base64)
{
    std::stringstream(base64) >> *this;
}

base64::base64(const data_chunk& value)
  : value_(value)
{
}

base64::base64(const base64& other)
  : base64(other.value_)
{
}

base64::operator const data_chunk&() const
{
    return value_;
}

base64::operator data_slice() const
{
    return value_;
}

std::istream& operator>>(std::istream& input, base64& argument)
{
    std::string base64;
    input >> base64;

    if (!decode_base64(argument.value_, base64))
    {
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(base64));
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, const base64& argument)
{
    output << encode_base64(argument.value_);
    return output;
}

} // namespace config
} // namespace libbitcoin
