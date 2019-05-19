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
#include <bitcoin/bitcoin/message/send_headers.hpp>

#include <bitcoin/bitcoin/message/version.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>

namespace libbitcoin {
namespace message {

const std::string send_headers::command = "sendheaders";
const uint32_t send_headers::version_minimum = version::level::bip130;
const uint32_t send_headers::version_maximum = version::level::maximum;

send_headers send_headers::factory_from_data(uint32_t version,
    const data_chunk& data)
{
    send_headers instance;
    instance.from_data(version, data);
    return instance;
}

send_headers send_headers::factory_from_data(uint32_t version,
    std::istream& stream)
{
    send_headers instance;
    instance.from_data(version, stream);
    return instance;
}

send_headers send_headers::factory_from_data(uint32_t version,
    reader& source)
{
    send_headers instance;
    instance.from_data(version, source);
    return instance;
}

size_t send_headers::satoshi_fixed_size(uint32_t version)
{
    return 0;
}

// This is a default instance so is invalid.
// The only way to make this valid is to deserialize it :/.
send_headers::send_headers()
  : insufficient_version_(true)
{
}

// protected
send_headers::send_headers(bool insufficient_version)
  : insufficient_version_(insufficient_version)
{
}

send_headers::send_headers(const send_headers& other)
  : send_headers(other.insufficient_version_)
{
}

send_headers::send_headers(send_headers&& other)
  : send_headers(other.insufficient_version_)
{
}

bool send_headers::is_valid() const
{
    return !insufficient_version_;
}

// This is again a default instance so is invalid.
void send_headers::reset()
{
    insufficient_version_ = true;
}

bool send_headers::from_data(uint32_t version, const data_chunk& data)
{
    data_source istream(data);
    return from_data(version, istream);
}

bool send_headers::from_data(uint32_t version, std::istream& stream)
{
    istream_reader source(stream);
    return from_data(version, source);
}

bool send_headers::from_data(uint32_t version, reader& source)
{
    reset();

    // Initialize as valid from deserialization.
    insufficient_version_ = false;

    if (version < send_headers::version_minimum)
    {
        insufficient_version_ = true;
        source.invalidate();
    }

    if (!source)
        reset();

    return source;
}

data_chunk send_headers::to_data(uint32_t version) const
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

void send_headers::to_data(uint32_t version, std::ostream& stream) const
{
}

size_t send_headers::serialized_size(uint32_t version) const
{
    return send_headers::satoshi_fixed_size(version);
}

} // namespace message
} // namespace libbitcoin
