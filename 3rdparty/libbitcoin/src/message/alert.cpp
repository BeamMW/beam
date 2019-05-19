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
#include <bitcoin/bitcoin/message/alert.hpp>

#include <bitcoin/bitcoin/math/limits.hpp>
#include <bitcoin/bitcoin/message/messages.hpp>
#include <bitcoin/bitcoin/message/version.hpp>
#include <bitcoin/bitcoin/utility/assert.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>

namespace libbitcoin {
namespace message {

const std::string alert::command = "alert";
const uint32_t alert::version_minimum = version::level::minimum;
const uint32_t alert::version_maximum = version::level::maximum;

alert alert::factory_from_data(uint32_t version, const data_chunk& data)
{
    alert instance;
    instance.from_data(version, data);
    return instance;
}

alert alert::factory_from_data(uint32_t version, std::istream& stream)
{
    alert instance;
    instance.from_data(version, stream);
    return instance;
}

alert alert::factory_from_data(uint32_t version, reader& source)
{
    alert instance;
    instance.from_data(version, source);
    return instance;
}

alert::alert()
  : payload_(), signature_()
{
}

alert::alert(const data_chunk& payload, const data_chunk& signature)
  : payload_(payload), signature_(signature)
{
}

alert::alert(data_chunk&& payload, data_chunk&& signature)
  : payload_(std::move(payload)), signature_(std::move(signature))
{
}

alert::alert(const alert& other)
  : alert(other.payload_, other.signature_)
{
}

alert::alert(alert&& other)
  : alert(std::move(other.payload_), std::move(other.signature_))
{
}

bool alert::is_valid() const
{
    return !payload_.empty() || !signature_.empty();
}

void alert::reset()
{
    payload_.clear();
    payload_.shrink_to_fit();
    signature_.clear();
    signature_.shrink_to_fit();
}

bool alert::from_data(uint32_t version, const data_chunk& data)
{
    data_source istream(data);
    return from_data(version, istream);
}

bool alert::from_data(uint32_t version, std::istream& stream)
{
    istream_reader source(stream);
    return from_data(version, source);
}

bool alert::from_data(uint32_t version, reader& source)
{
    reset();

    payload_ = source.read_bytes(source.read_size_little_endian());
    signature_ = source.read_bytes(source.read_size_little_endian());

    if (!source)
        reset();

    return source;
}

data_chunk alert::to_data(uint32_t version) const
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

void alert::to_data(uint32_t version, std::ostream& stream) const
{
    ostream_writer sink(stream);
    to_data(version, sink);
}

void alert::to_data(uint32_t version, writer& sink) const
{
    sink.write_variable_little_endian(payload_.size());
    sink.write_bytes(payload_);
    sink.write_variable_little_endian(signature_.size());
    sink.write_bytes(signature_);
}

size_t alert::serialized_size(uint32_t version) const
{
    return message::variable_uint_size(payload_.size()) + payload_.size() +
        message::variable_uint_size(signature_.size()) + signature_.size();
}

data_chunk& alert::payload()
{
    return payload_;
}

const data_chunk& alert::payload() const
{
    return payload_;
}

void alert::set_payload(const data_chunk& value)
{
    payload_ = value;
}

void alert::set_payload(data_chunk&& value)
{
    payload_ = std::move(value);
}

data_chunk& alert::signature()
{
    return signature_;
}

const data_chunk& alert::signature() const
{
    return signature_;
}

void alert::set_signature(const data_chunk& value)
{
    signature_ = value;
}

void alert::set_signature(data_chunk&& value)
{
    signature_ = std::move(value);
}

alert& alert::operator=(alert&& other)
{
    payload_ = std::move(other.payload_);
    signature_ = std::move(other.signature_);
    return *this;
}

bool alert::operator==(const alert& other) const
{
    return (payload_ == other.payload_)
        && (signature_ == other.signature_);
}

bool alert::operator!=(const alert& other) const
{
    return !(*this == other);
}

} // namespace message
} // namespace libbitcoin
