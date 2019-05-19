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
#include <bitcoin/bitcoin/message/header.hpp>

#include <cstdint>
#include <cstddef>
#include <istream>
#include <utility>
#include <bitcoin/bitcoin/chain/header.hpp>
#include <bitcoin/bitcoin/chain/transaction.hpp>
#include <bitcoin/bitcoin/message/messages.hpp>
#include <bitcoin/bitcoin/message/version.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>

namespace libbitcoin {
namespace message {

const std::string header::command = "headers";
const uint32_t header::version_minimum = version::level::minimum;
const uint32_t header::version_maximum = version::level::maximum;

header header::factory_from_data(uint32_t version, const data_chunk& data)
{
    header instance;
    instance.from_data(version, data);
    return instance;
}

header header::factory_from_data(uint32_t version, std::istream& stream)
{
    header instance;
    instance.from_data(version, stream);
    return instance;
}

header header::factory_from_data(uint32_t version, reader& source)
{
    header instance;
    instance.from_data(version, source);
    return instance;
}

size_t header::satoshi_fixed_size(uint32_t version)
{
    const auto canonical = (version == version::level::canonical);
    return chain::header::satoshi_fixed_size() +
        (canonical ? 0 : message::variable_uint_size(0));
}

header::header()
  : chain::header()
{
}

header::header(uint32_t version,
    const hash_digest& previous_block_hash, const hash_digest& merkle,
    uint32_t timestamp, uint32_t bits, uint32_t nonce)
  : chain::header(version, previous_block_hash, merkle, timestamp, bits, nonce)
{
}

header::header(uint32_t version,
    hash_digest&& previous_block_hash, hash_digest&& merkle,
    uint32_t timestamp, uint32_t bits, uint32_t nonce)
  : chain::header(version, std::move(previous_block_hash), std::move(merkle),
      timestamp, bits, nonce)
{
}

header::header(const chain::header& other)
  : chain::header(other)
{
}

header::header(chain::header&& other)
  : chain::header(std::move(other))
{
}

header::header(const header& other)
  : chain::header(other)
{
}

header::header(header&& other)
  : chain::header(std::move(other))
{
}

bool header::from_data(uint32_t version, const data_chunk& data)
{
    data_source istream(data);
    return from_data(version, istream);
}

bool header::from_data(uint32_t version, std::istream& stream)
{
    istream_reader source(stream);
    return from_data(version, source);
}

bool header::from_data(uint32_t version, reader& source)
{
    if (!chain::header::from_data(source))
        return false;

    // The header message must trail a zero byte (yes, it's stoopid).
    // bitcoin.org/en/developer-reference#headers
    if (version != version::level::canonical && source.read_byte() != 0x00)
        source.invalidate();

    if (!source)
        reset();

    return source;
}

data_chunk header::to_data(uint32_t version) const
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

void header::to_data(uint32_t version, std::ostream& stream) const
{
    ostream_writer sink(stream);
    to_data(version, sink);
}

void header::to_data(uint32_t version, writer& sink) const
{
    chain::header::to_data(sink);

    if (version != version::level::canonical)
        sink.write_variable_little_endian(0);
}

void header::reset()
{
    chain::header::reset();
}

size_t header::serialized_size(uint32_t version) const
{
    return satoshi_fixed_size(version);
}

header& header::operator=(chain::header&& other)
{
    chain::header::operator=(std::move(other));
    return *this;
}

header& header::operator=(header&& other)
{
    chain::header::operator=(std::move(other));
    return *this;
}

header& header::operator=(const header& other)
{
    chain::header::operator=(other);
    return *this;
}

bool header::operator==(const chain::header& other) const
{
    return chain::header::operator==(other);
}

bool header::operator!=(const chain::header& other) const
{
    return chain::header::operator!=(other);
}

bool header::operator==(const header& other) const
{
    return chain::header::operator==(other);
}

bool header::operator!=(const header& other) const
{
    return chain::header::operator!=(other);
}

} // namespace message
} // namespace libbitcoin
