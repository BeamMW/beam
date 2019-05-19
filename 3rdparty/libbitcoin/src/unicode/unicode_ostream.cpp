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
#include <bitcoin/bitcoin/unicode/unicode_ostream.hpp>

#include <cstddef>
#include <iostream>
#include <bitcoin/bitcoin/unicode/unicode_streambuf.hpp>

namespace libbitcoin {

unicode_ostream::unicode_ostream(std::ostream& narrow_stream,
    std::wostream& wide_stream, size_t size)
#ifdef _MSC_VER
  : std::ostream(new unicode_streambuf(wide_stream.rdbuf(), size))
#else
  : std::ostream(narrow_stream.rdbuf())
#endif
{
}

unicode_ostream::~unicode_ostream()
{
#ifdef _MSC_VER
    delete rdbuf();
#endif
}

} // namespace libbitcoin
