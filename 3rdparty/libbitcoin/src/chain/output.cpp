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
#include <bitcoin/bitcoin/chain/output.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <bitcoin/bitcoin/constants.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>
#include <bitcoin/bitcoin/wallet/payment_address.hpp>

namespace libbitcoin {
namespace chain {

using namespace bc::wallet;

// This is a consensus critical value that must be set on reset.
const uint64_t output::not_found = sighash_null_value;

// This is a non-consensus sentinel used to indicate an output is unspent.
const uint32_t output::validation::not_spent = max_uint32;

// Constructors.
//-----------------------------------------------------------------------------

output::output()
  : value_(not_found),
    script_{},
    validation{}
{
}

output::output(output&& other)
  : addresses_(other.addresses_cache()),
    value_(other.value_),
    script_(std::move(other.script_)),
    validation(other.validation)
{
}

output::output(const output& other)
  : addresses_(other.addresses_cache()),
    value_(other.value_),
    script_(other.script_),
    validation(other.validation)
{
}

output::output(uint64_t value, chain::script&& script)
  : value_(value),
    script_(std::move(script)),
    validation{}
{
}

output::output(uint64_t value, const chain::script& script)
  : value_(value),
    script_(script),
    validation{}
{
}

// Private cache access for copy/move construction.
output::addresses_ptr output::addresses_cache() const
{
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    shared_lock lock(mutex_);

    return addresses_;
    ///////////////////////////////////////////////////////////////////////////
}

// Operators.
//-----------------------------------------------------------------------------

output& output::operator=(output&& other)
{
    addresses_ = other.addresses_cache();
    value_ = other.value_;
    script_ = std::move(other.script_);
    validation = std::move(other.validation);
    return *this;
}

output& output::operator=(const output& other)
{
    addresses_ = other.addresses_cache();
    value_ = other.value_;
    script_ = other.script_;
    validation = other.validation;
    return *this;
}

bool output::operator==(const output& other) const
{
    return (value_ == other.value_) && (script_ == other.script_);
}

bool output::operator!=(const output& other) const
{
    return !(*this == other);
}

// Deserialization.
//-----------------------------------------------------------------------------

output output::factory_from_data(const data_chunk& data, bool wire)
{
    output instance;
    instance.from_data(data, wire);
    return instance;
}

output output::factory_from_data(std::istream& stream, bool wire)
{
    output instance;
    instance.from_data(stream, wire);
    return instance;
}

output output::factory_from_data(reader& source, bool wire)
{
    output instance;
    instance.from_data(source, wire);
    return instance;
}

bool output::from_data(const data_chunk& data, bool wire)
{
    data_source istream(data);
    return from_data(istream, wire);
}

bool output::from_data(std::istream& stream, bool wire)
{
    istream_reader source(stream);
    return from_data(source, wire);
}

bool output::from_data(reader& source, bool wire, bool)
{
    reset();

    if (!wire)
        validation.spender_height = source.read_4_bytes_little_endian();

    value_ = source.read_8_bytes_little_endian();
    script_.from_data(source, true);

    if (!source)
        reset();

    return source;
}

// protected
void output::reset()
{
    value_ = output::not_found;
    script_.reset();
}

// Empty scripts are valid, validation relies on not_found only.
bool output::is_valid() const
{
    return value_ != output::not_found;
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk output::to_data(bool wire) const
{
    data_chunk data;
    const auto size = serialized_size(wire);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, wire);
    ostream.flush();
    BITCOIN_ASSERT(data.size() == size);
    return data;
}

void output::to_data(std::ostream& stream, bool wire) const
{
    ostream_writer sink(stream);
    to_data(sink, wire);
}

void output::to_data(writer& sink, bool wire, bool) const
{
    if (!wire)
    {
        auto height32 = safe_unsigned<uint32_t>(validation.spender_height);
        sink.write_4_bytes_little_endian(height32);
    }

    sink.write_8_bytes_little_endian(value_);
    script_.to_data(sink, true);
}

// Size.
//-----------------------------------------------------------------------------

size_t output::serialized_size(bool wire) const
{
    // validation.spender_height is size_t stored as uint32_t.
    return (wire ? 0 : sizeof(uint32_t)) + sizeof(value_) +
        script_.serialized_size(true);
}

// Accessors.
//-----------------------------------------------------------------------------

uint64_t output::value() const
{
    return value_;
}

void output::set_value(uint64_t value)
{
    value_ = value;
}

const chain::script& output::script() const
{
    return script_;
}

void output::set_script(const chain::script& value)
{
    script_ = value;
    invalidate_cache();
}

void output::set_script(chain::script&& value)
{
    script_ = std::move(value);
    invalidate_cache();
}

// protected
void output::invalidate_cache() const
{
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if (addresses_)
    {
        mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        addresses_.reset();
        //---------------------------------------------------------------------
        mutex_.unlock_and_lock_upgrade();
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
}

payment_address output::address(uint8_t p2kh_version,
    uint8_t p2sh_version) const
{
    const auto value = addresses(p2kh_version, p2sh_version);
    return value.empty() ? payment_address{} : value.front();
}

payment_address::list output::addresses(uint8_t p2kh_version,
    uint8_t p2sh_version) const
{
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if (!addresses_)
    {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        mutex_.unlock_upgrade_and_lock();
        addresses_ = std::make_shared<payment_address::list>(
            payment_address::extract_output(script_, p2kh_version,
                p2sh_version));
        mutex_.unlock_and_lock_upgrade();
        //---------------------------------------------------------------------
    }

    const auto addresses = *addresses_;
    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    return addresses;
}

// Validation helpers.
//-----------------------------------------------------------------------------

size_t output::signature_operations(bool bip141) const
{
    // Penalize quadratic signature operations (bip141).
    const auto sigops_factor = bip141 ? fast_sigops_factor : 1u;

    // Count heavy sigops in the output script.
    return script_.sigops(false) * sigops_factor;
}

bool output::is_dust(uint64_t minimum_value) const
{
    // If provably unspendable it does not expand the unspent output set.
    return value_ < minimum_value && !script_.is_unspendable();
}

bool output::extract_committed_hash(hash_digest& out) const
{
    const auto& ops = script_.operations();

    if (!script::is_commitment_pattern(ops))
        return false;

    // The four byte offset for the witness commitment hash (bip141).
    const auto start = ops[1].data().begin() + sizeof(witness_head);
    std::copy_n(start, hash_size, out.begin());
    return true;
}

} // namespace chain
} // namespace libbitcoin