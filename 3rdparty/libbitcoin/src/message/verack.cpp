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
#include <bitcoin/bitcoin/message/verack.hpp>

#include <bitcoin/bitcoin/message/version.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>

namespace libbitcoin {
namespace message {

const std::string verack::command = "verack";
const uint32_t verack::version_minimum = version::level::minimum;
const uint32_t verack::version_maximum = version::level::maximum;

verack verack::factory_from_data(uint32_t version,
    const data_chunk& data)
{
    verack instance;
    instance.from_data(version, data);
    return instance;
}

verack verack::factory_from_data(uint32_t version,
    std::istream& stream)
{
    verack instance;
    instance.from_data(version, stream);
    return instance;
}

verack verack::factory_from_data(uint32_t version,
    reader& source)
{
    verack instance;
    instance.from_data(version, source);
    return instance;
}

verack::verack()
{
}

bool verack::is_valid() const
{
    return true;
}

void verack::reset()
{
}

bool verack::from_data(uint32_t version, const data_chunk& data)
{
    data_source istream(data);
    return from_data(version, istream);
}

bool verack::from_data(uint32_t version, std::istream& stream)
{
    istream_reader source(stream);
    return from_data(version, source);
}

bool verack::from_data(uint32_t version, reader& source)
{
    reset();
    return source;
}

data_chunk verack::to_data(uint32_t version) const
{
    data_chunk data;
    const auto size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    BITCOIN_ASSERT(data.size() == size);
    return data;
}

void verack::to_data(uint32_t version, std::ostream& stream) const
{
}

size_t verack::serialized_size(uint32_t version) const
{
    return verack::satoshi_fixed_size(version);
}


size_t verack::satoshi_fixed_size(uint32_t version)
{
    return 0;
}

} // namespace message
} // namespace libbitcoin
