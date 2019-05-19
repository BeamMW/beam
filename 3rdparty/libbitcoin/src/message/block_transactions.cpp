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
#include <bitcoin/bitcoin/message/block_transactions.hpp>

#include <bitcoin/bitcoin/math/limits.hpp>
#include <bitcoin/bitcoin/message/messages.hpp>
#include <bitcoin/bitcoin/message/version.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>

namespace libbitcoin {
namespace message {

const std::string block_transactions::command = "blocktxn";
const uint32_t block_transactions::version_minimum = version::level::bip152;
const uint32_t block_transactions::version_maximum = version::level::bip152;

block_transactions block_transactions::factory_from_data(uint32_t version,
    const data_chunk& data)
{
    block_transactions instance;
    instance.from_data(version, data);
    return instance;
}

block_transactions block_transactions::factory_from_data(uint32_t version,
    std::istream& stream)
{
    block_transactions instance;
    instance.from_data(version, stream);
    return instance;
}

block_transactions block_transactions::factory_from_data(uint32_t version,
    reader& source)
{
    block_transactions instance;
    instance.from_data(version, source);
    return instance;
}

block_transactions::block_transactions()
  : block_hash_(null_hash), transactions_()
{
}

block_transactions::block_transactions(const hash_digest& block_hash,
    const chain::transaction::list& transactions)
  : block_hash_(block_hash), transactions_(transactions)
{
}

block_transactions::block_transactions(hash_digest&& block_hash,
    chain::transaction::list&& transactions)
  : block_hash_(std::move(block_hash)), transactions_(std::move(transactions))
{
}

block_transactions::block_transactions(const block_transactions& other)
  : block_transactions(other.block_hash_, other.transactions_)
{
}

block_transactions::block_transactions(block_transactions&& other)
  : block_transactions(std::move(other.block_hash_),
      std::move(other.transactions_))
{
}

bool block_transactions::is_valid() const
{
    return (block_hash_ != null_hash);
}

void block_transactions::reset()
{
    block_hash_ = null_hash;
    transactions_.clear();
    transactions_.shrink_to_fit();
}

bool block_transactions::from_data(uint32_t version,
    const data_chunk& data)
{
    data_source istream(data);
    return from_data(version, istream);
}

bool block_transactions::from_data(uint32_t version,
    std::istream& stream)
{
    istream_reader source(stream);
    return from_data(version, source);
}

bool block_transactions::from_data(uint32_t version, reader& source)
{
    reset();

    block_hash_ = source.read_hash();
    const auto count = source.read_size_little_endian();

    // Guard against potential for arbitary memory allocation.
    if (count > max_block_size)
        source.invalidate();
    else
        transactions_.resize(count);

    // Order is required.
    for (auto& tx: transactions_)
        if (!tx.from_data(source, true))
            break;

    if (version < block_transactions::version_minimum)
        source.invalidate();

    if (!source)
        reset();

    return source;
}

data_chunk block_transactions::to_data(uint32_t version) const
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

void block_transactions::to_data(uint32_t version,
    std::ostream& stream) const
{
    ostream_writer sink(stream);
    to_data(version, sink);
}

void block_transactions::to_data(uint32_t version, writer& sink) const
{
    sink.write_hash(block_hash_);
    sink.write_variable_little_endian(transactions_.size());

    for (const auto& element: transactions_)
        element.to_data(sink);
}

size_t block_transactions::serialized_size(uint32_t version) const
{
    auto size = hash_size + message::variable_uint_size(transactions_.size());

    for (const auto& element: transactions_)
        size += element.serialized_size(true);

    return size;
}

hash_digest& block_transactions::block_hash()
{
    return block_hash_;
}

const hash_digest& block_transactions::block_hash() const
{
    return block_hash_;
}

void block_transactions::set_block_hash(const hash_digest& value)
{
    block_hash_ = value;
}

void block_transactions::set_block_hash(hash_digest&& value)
{
    block_hash_ = std::move(value);
}

chain::transaction::list& block_transactions::transactions()
{
    return transactions_;
}

const chain::transaction::list& block_transactions::transactions() const
{
    return transactions_;
}

void block_transactions::set_transactions(const chain::transaction::list& value)
{
    transactions_ = value;
}

void block_transactions::set_transactions(chain::transaction::list&& value)
{
    transactions_ = std::move(value);
}

block_transactions& block_transactions::operator=(block_transactions&& other)
{
    block_hash_ = std::move(other.block_hash_);
    transactions_ = std::move(other.transactions_);
    return *this;
}

bool block_transactions::operator==(const block_transactions& other) const
{
    return (block_hash_ == other.block_hash_)
        && (transactions_ == other.transactions_);
}

bool block_transactions::operator!=(const block_transactions& other) const
{
    return !(*this == other);
}

} // namespace message
} // namespace libbitcoin
