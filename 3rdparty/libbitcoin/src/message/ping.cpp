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
#include <bitcoin/bitcoin/message/ping.hpp>

#include <bitcoin/bitcoin/message/version.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>

namespace libbitcoin {
namespace message {

const std::string ping::command = "ping";
const uint32_t ping::version_minimum = version::level::minimum;
const uint32_t ping::version_maximum = version::level::maximum;

ping ping::factory_from_data(uint32_t version, const data_chunk& data)
{
    ping instance;
    instance.from_data(version, data);
    return instance;
}

ping ping::factory_from_data(uint32_t version, std::istream& stream)
{
    ping instance;
    instance.from_data(version, stream);
    return instance;
}

ping ping::factory_from_data(uint32_t version, reader& source)
{
    ping instance;
    instance.from_data(version, source);
    return instance;
}

size_t ping::satoshi_fixed_size(uint32_t version)
{
    return version < version::level::bip31 ? 0 : sizeof(nonce_);
}

ping::ping()
  : nonce_(0), nonceless_(false), valid_(false)
{
}

ping::ping(uint64_t nonce)
  : nonce_(nonce), nonceless_(false), valid_(true)
{
}

ping::ping(const ping& other)
  : nonce_(other.nonce_), nonceless_(other.nonceless_), valid_(other.valid_)
{
}

bool ping::from_data(uint32_t version, const data_chunk& data)
{
    data_source istream(data);
    return from_data(version, istream);
}

bool ping::from_data(uint32_t version, std::istream& stream)
{
    istream_reader source(stream);
    return from_data(version, source);
}

bool ping::from_data(uint32_t version, reader& source)
{
    reset();

    valid_ = true;
    nonceless_ = (version < version::level::bip31);

    if (!nonceless_)
        nonce_ = source.read_8_bytes_little_endian();

    if (!source)
        reset();

    return source;
}

data_chunk ping::to_data(uint32_t version) const
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

void ping::to_data(uint32_t version, std::ostream& stream) const
{
    ostream_writer sink(stream);
    to_data(version, sink);
}

void ping::to_data(uint32_t version, writer& sink) const
{
    if (version >= version::level::bip31)
        sink.write_8_bytes_little_endian(nonce_);
}

bool ping::is_valid() const
{
    return valid_ || nonceless_ || nonce_ != 0;
}

void ping::reset()
{
    nonce_ = 0;
    nonceless_ = false;
    valid_ = false;
}

size_t ping::serialized_size(uint32_t version) const
{
    return satoshi_fixed_size(version);
}

uint64_t ping::nonce() const
{
    return nonce_;
}

void ping::set_nonce(uint64_t value)
{
    nonce_ = value;
}

ping& ping::operator=(ping&& other)
{
    nonce_ = other.nonce_;
    return *this;
}

bool ping::operator==(const ping& other) const
{
    // Nonce should be zero if not used.
    return (nonce_ == other.nonce_);
}

bool ping::operator!=(const ping& other) const
{
    // Nonce should be zero if not used.
    return !(*this == other);
}

} // namespace message
} // namespace libbitcoin
