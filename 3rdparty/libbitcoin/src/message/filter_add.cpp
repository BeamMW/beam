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
#include <bitcoin/bitcoin/message/filter_add.hpp>

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

const std::string filter_add::command = "filteradd";
const uint32_t filter_add::version_minimum = version::level::bip37;
const uint32_t filter_add::version_maximum = version::level::maximum;

filter_add filter_add::factory_from_data(uint32_t version,
    const data_chunk& data)
{
    filter_add instance;
    instance.from_data(version, data);
    return instance;
}

filter_add filter_add::factory_from_data(uint32_t version,
    std::istream& stream)
{
    filter_add instance;
    instance.from_data(version, stream);
    return instance;
}

filter_add filter_add::factory_from_data(uint32_t version,
    reader& source)
{
    filter_add instance;
    instance.from_data(version, source);
    return instance;
}

filter_add::filter_add()
  : data_()
{
}

filter_add::filter_add(const data_chunk& data)
  : data_(data)
{
}

filter_add::filter_add(data_chunk&& data)
  : data_(std::move(data))
{
}

filter_add::filter_add(const filter_add& other)
  : filter_add(other.data_)
{
}

filter_add::filter_add(filter_add&& other)
  : filter_add(std::move(other.data_))
{
}

bool filter_add::is_valid() const
{
    return !data_.empty();
}

void filter_add::reset()
{
    data_.clear();
    data_.shrink_to_fit();
}

bool filter_add::from_data(uint32_t version, const data_chunk& data)
{
    data_source istream(data);
    return from_data(version, istream);
}

bool filter_add::from_data(uint32_t version, std::istream& stream)
{
    istream_reader source(stream);
    return from_data(version, source);
}

bool filter_add::from_data(uint32_t version, reader& source)
{
    reset();

    const auto size = source.read_size_little_endian();

    if (size > max_filter_add)
        source.invalidate();
    else
        data_ = source.read_bytes(size);

    if (version < filter_add::version_minimum)
        source.invalidate();

    if (!source)
        reset();

    return source;
}

data_chunk filter_add::to_data(uint32_t version) const
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

void filter_add::to_data(uint32_t version, std::ostream& stream) const
{
    ostream_writer sink(stream);
    to_data(version, sink);
}

void filter_add::to_data(uint32_t version, writer& sink) const
{
    sink.write_variable_little_endian(data_.size());
    sink.write_bytes(data_);
}

size_t filter_add::serialized_size(uint32_t version) const
{
    return message::variable_uint_size(data_.size()) + data_.size();
}

data_chunk& filter_add::data()
{
    return data_;
}

const data_chunk& filter_add::data() const
{
    return data_;
}

void filter_add::set_data(const data_chunk& value)
{
    data_ = value;
}

void filter_add::set_data(data_chunk&& value)
{
    data_ = std::move(value);
}

filter_add& filter_add::operator=(filter_add&& other)
{
    data_ = std::move(other.data_);
    return *this;
}

bool filter_add::operator==(const filter_add& other) const
{
    return (data_ == other.data_);
}

bool filter_add::operator!=(const filter_add& other) const
{
    return !(*this == other);
}

} // namespace message
} // namespace libbitcoin
