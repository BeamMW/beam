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
#include <bitcoin/bitcoin/message/filter_clear.hpp>

#include <bitcoin/bitcoin/message/version.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>

namespace libbitcoin {
namespace message {

const std::string filter_clear::command = "filterclear";
const uint32_t filter_clear::version_minimum = version::level::bip37;
const uint32_t filter_clear::version_maximum = version::level::maximum;

filter_clear filter_clear::factory_from_data(uint32_t version,
    const data_chunk& data)
{
    filter_clear instance;
    instance.from_data(version, data);
    return instance;
}

filter_clear filter_clear::factory_from_data(uint32_t version,
    std::istream& stream)
{
    filter_clear instance;
    instance.from_data(version, stream);
    return instance;
}

filter_clear filter_clear::factory_from_data(uint32_t version,
    reader& source)
{
    filter_clear instance;
    instance.from_data(version, source);
    return instance;
}

// This is a default instance so is invalid.
// The only way to make this valid is to deserialize it :/.
filter_clear::filter_clear()
  : insufficient_version_(true)
{
}

// protected
filter_clear::filter_clear(bool insufficient_version)
  : insufficient_version_(insufficient_version)
{
}

filter_clear::filter_clear(const filter_clear& other)
  : filter_clear(other.insufficient_version_)
{
}

filter_clear::filter_clear(filter_clear&& other)
  : filter_clear(other.insufficient_version_)
{
}


bool filter_clear::is_valid() const
{
    return !insufficient_version_;
}

// This is again a default instance so is invalid.
void filter_clear::reset()
{
    insufficient_version_ = true;
}

bool filter_clear::from_data(uint32_t version, const data_chunk& data)
{
    data_source istream(data);
    return from_data(version, istream);
}

bool filter_clear::from_data(uint32_t version, std::istream& stream)
{
    istream_reader source(stream);
    return from_data(version, source);
}

bool filter_clear::from_data(uint32_t version, reader& source)
{
    reset();

    // Initialize as valid from deserialization.
    insufficient_version_ = false;

    if (version < filter_clear::version_minimum)
        source.invalidate();

    if (!source)
        reset();

    return source;
}

data_chunk filter_clear::to_data(uint32_t version) const
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

void filter_clear::to_data(uint32_t version, std::ostream& stream) const
{
    ostream_writer sink(stream);
    to_data(version, sink);
}

void filter_clear::to_data(uint32_t version, writer& sink) const
{
}

size_t filter_clear::serialized_size(uint32_t version) const
{
    return filter_clear::satoshi_fixed_size(version);
}

size_t filter_clear::satoshi_fixed_size(uint32_t version)
{
    return 0;
}

} // namespace message
} // namespace libbitcoin
