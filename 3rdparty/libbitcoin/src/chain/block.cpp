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
#include <bitcoin/bitcoin/chain/block.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <cfenv>
#include <cmath>
#include <iterator>
#include <memory>
#include <numeric>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <boost/range/adaptor/reversed.hpp>
#include <bitcoin/bitcoin/chain/chain_state.hpp>
#include <bitcoin/bitcoin/chain/compact.hpp>
#include <bitcoin/bitcoin/chain/input_point.hpp>
#include <bitcoin/bitcoin/chain/script.hpp>
#include <bitcoin/bitcoin/config/checkpoint.hpp>
#include <bitcoin/bitcoin/constants.hpp>
#include <bitcoin/bitcoin/error.hpp>
#include <bitcoin/bitcoin/formats/base_16.hpp>
#include <bitcoin/bitcoin/math/hash.hpp>
#include <bitcoin/bitcoin/math/limits.hpp>
#include <bitcoin/bitcoin/machine/number.hpp>
#include <bitcoin/bitcoin/machine/opcode.hpp>
#include <bitcoin/bitcoin/machine/rule_fork.hpp>
#include <bitcoin/bitcoin/message/messages.hpp>
#include <bitcoin/bitcoin/utility/asio.hpp>
#include <bitcoin/bitcoin/utility/assert.hpp>
#include <bitcoin/bitcoin/utility/container_sink.hpp>
#include <bitcoin/bitcoin/utility/container_source.hpp>
#include <bitcoin/bitcoin/utility/istream_reader.hpp>
#include <bitcoin/bitcoin/utility/ostream_writer.hpp>

namespace libbitcoin {
namespace chain {

using namespace bc::config;
using namespace bc::machine;
using namespace boost::adaptors;

static const std::string encoded_mainnet_genesis_block =
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "29ab5f49"
    "ffff001d"
    "1dac2b7c"
    "01"
    "01000000"
    "01"
    "0000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "4d"
    "04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73"
    "ffffffff"
    "01"
    "00f2052a01000000"
    "43"
    "4104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac"
    "00000000";

static const std::string encoded_testnet_genesis_block =
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "dae5494d"
    "ffff001d"
    "1aa4ae18"
    "01"
    "01000000"
    "01"
    "0000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "4d"
    "04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73"
    "ffffffff"
    "01"
    "00f2052a01000000"
    "43"
    "4104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac"
    "00000000";

static const std::string encoded_regtest_genesis_block =
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "dae5494d"
    "ffff7f20"
    "02000000"
    "01"
    "01000000"
    "01"
    "0000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "4d"
    "04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73"
    "ffffffff"
    "01"
    "00f2052a01000000"
    "43"
    "4104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac"
    "00000000";

// Constructors.
//-----------------------------------------------------------------------------

block::block()
  : header_{}, validation{}
{
}

block::block(const block& other)
  : block(other.header_, other.transactions_)
{
    validation = other.validation;
}

block::block(block&& other)
  : block(std::move(other.header_), std::move(other.transactions_))
{
    validation = std::move(other.validation);
}

// TODO: deal with possibility of inconsistent merkle root in relation to txs.
block::block(const chain::header& header,
    const transaction::list& transactions)
  : header_(header), transactions_(transactions), validation{}
{
}

// TODO: deal with possibility of inconsistent merkle root in relation to txs.
block::block(chain::header&& header, transaction::list&& transactions)
  : header_(std::move(header)), transactions_(std::move(transactions)),
    validation{}
{
}

// Operators.
//-----------------------------------------------------------------------------

block& block::operator=(block&& other)
{
    header_ = std::move(other.header_);
    transactions_ = std::move(other.transactions_);
    validation = std::move(other.validation);
    return *this;
}

bool block::operator==(const block& other) const
{
    return (header_ == other.header_) && (transactions_ == other.transactions_);
}

bool block::operator!=(const block& other) const
{
    return !(*this == other);
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
block block::factory_from_data(const data_chunk& data, bool witness)
{
    block instance;
    instance.from_data(data, witness);
    return instance;
}

// static
block block::factory_from_data(std::istream& stream, bool witness)
{
    block instance;
    instance.from_data(stream, witness);
    return instance;
}

// static
block block::factory_from_data(reader& source, bool witness)
{
    block instance;
    instance.from_data(source, witness);
    return instance;
}

bool block::from_data(const data_chunk& data, bool witness)
{
    data_source istream(data);
    return from_data(istream, witness);
}

bool block::from_data(std::istream& stream, bool witness)
{
    istream_reader source(stream);
    return from_data(source, witness);
}

// Full block deserialization is always canonical encoding.
bool block::from_data(reader& source, bool witness)
{
    validation.start_deserialize = asio::steady_clock::now();
    reset();

    if (!header_.from_data(source, true))
        return false;

    const auto count = source.read_size_little_endian();

    // Guard against potential for arbitary memory allocation.
    if (count > max_block_size)
        source.invalidate();
    else
        transactions_.resize(count);

    // Order is required, explicit loop allows early termination.
    for (auto& tx: transactions_)
        if (!tx.from_data(source, true, witness))
            break;

    // TODO: optimize by having reader skip witness data.
    if (!witness)
        strip_witness();

    if (!source)
        reset();

    validation.end_deserialize = asio::steady_clock::now();
    return source;
}

// private
void block::reset()
{
    header_.reset();
    transactions_.clear();
    transactions_.shrink_to_fit();
}

bool block::is_valid() const
{
    return !transactions_.empty() || header_.is_valid();
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk block::to_data(bool witness) const
{
    data_chunk data;
    const auto size = serialized_size(witness);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream);
    ostream.flush();
    BITCOIN_ASSERT(data.size() == size);
    return data;
}

void block::to_data(std::ostream& stream, bool witness) const
{
    ostream_writer sink(stream);
    to_data(sink, witness);
}

// Full block serialization is always canonical encoding.
void block::to_data(writer& sink, bool witness) const
{
    header_.to_data(sink, true);
    sink.write_size_little_endian(transactions_.size());
    const auto to = [&sink, witness](const transaction& tx)
    {
        tx.to_data(sink, true, witness);
    };

    std::for_each(transactions_.begin(), transactions_.end(), to);
}

hash_list block::to_hashes(bool witness) const
{
    hash_list out;
    out.reserve(transactions_.size());
    const auto to_hash = [&out, witness](const transaction& tx)
    {
        out.push_back(tx.hash(witness));
    };

    // Hash ordering matters, don't use std::transform here.
    std::for_each(transactions_.begin(), transactions_.end(), to_hash);
    return out;
}

// Properties (size, accessors, cache).
//-----------------------------------------------------------------------------

// Full block serialization is always canonical encoding.
size_t block::serialized_size(bool witness) const
{
    size_t value;

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if (witness && total_size_ != boost::none)
    {
        value = total_size_.get();
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return value;
    }

    if (!witness && base_size_ != boost::none)
    {
        value = base_size_.get();
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return value;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    const auto sum = [witness](size_t total, const transaction& tx)
    {
        return safe_add(total, tx.serialized_size(true, witness));
    };

    const auto& txs = transactions_;
    value = header_.serialized_size(true) +
        message::variable_uint_size(transactions_.size()) +
        std::accumulate(txs.begin(), txs.end(), size_t(0), sum);

    if (witness)
        total_size_ = value;
    else
        base_size_ = value;

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    return value;
}

chain::header& block::header()
{
    return header_;
}

const chain::header& block::header() const
{
    return header_;
}

// TODO: must call header.set_merkle(generate_merkle_root()) though this may
// be very suboptimal if the block is being constructed. First verify that all
// current uses will not be impacted and if so change them to use constructor.
void block::set_header(const chain::header& value)
{
    header_ = value;
}

// TODO: see set_header comments.
void block::set_header(chain::header&& value)
{
    header_ = std::move(value);
}

transaction::list& block::transactions()
{
    return transactions_;
}

const transaction::list& block::transactions() const
{
    return transactions_;
}

// TODO: see set_header comments.
void block::set_transactions(const transaction::list& value)
{
    transactions_ = value;
    segregated_ = boost::none;
    total_inputs_ = boost::none;
    base_size_ = boost::none;
    total_size_ = boost::none;
}

// TODO: see set_header comments.
void block::set_transactions(transaction::list&& value)
{
    transactions_ = std::move(value);
    segregated_ = boost::none;
    total_inputs_ = boost::none;
    base_size_ = boost::none;
    total_size_ = boost::none;
}

// Convenience property.
hash_digest block::hash() const
{
    return header_.hash();
}

// Utilities.
//-----------------------------------------------------------------------------

chain::block block::genesis_mainnet()
{
    data_chunk data;
    decode_base16(data, encoded_mainnet_genesis_block);
    const auto genesis = chain::block::factory_from_data(data);

    BITCOIN_ASSERT(genesis.is_valid());
    BITCOIN_ASSERT(genesis.transactions().size() == 1);
    BITCOIN_ASSERT(genesis.generate_merkle_root() == genesis.header().merkle());
    return genesis;
}

chain::block block::genesis_testnet()
{
    data_chunk data;
    decode_base16(data, encoded_testnet_genesis_block);
    const auto genesis = chain::block::factory_from_data(data);

    BITCOIN_ASSERT(genesis.is_valid());
    BITCOIN_ASSERT(genesis.transactions().size() == 1);
    BITCOIN_ASSERT(genesis.generate_merkle_root() == genesis.header().merkle());
    return genesis;
}

chain::block block::genesis_regtest()
{
    data_chunk data;
    decode_base16(data, encoded_regtest_genesis_block);
    const auto genesis = chain::block::factory_from_data(data);

    BITCOIN_ASSERT(genesis.is_valid());
    BITCOIN_ASSERT(genesis.transactions().size() == 1);
    BITCOIN_ASSERT(genesis.generate_merkle_root() == genesis.header().merkle());
    return genesis;
}

// With a 32 bit chain the size of the result should not exceed 43 and with a
// 64 bit chain should not exceed 75, using a limit of: 10 + log2(height) + 1.
size_t block::locator_size(size_t top)
{
    // Set rounding behavior, not consensus-related, thread side effect :<.
    std::fesetround(FE_UPWARD);

    const auto first_ten_or_top = std::min(size_t(10), top);
    const auto remaining = top - first_ten_or_top;
    const auto back_off = remaining == 0 ? 0.0 : std::log2(remaining);
    const auto rounded_up_log = static_cast<size_t>(std::nearbyint(back_off));
    return first_ten_or_top + rounded_up_log + size_t(1);
}

// This algorithm is a network best practice, not a consensus rule.
block::indexes block::locator_heights(size_t top)
{
    size_t step = 1;
    block::indexes heights;
    const auto reservation = locator_size(top);
    heights.reserve(reservation);

    // Start at the top of the chain and work backwards to zero.
    for (auto height = top; height > 0; height = floor_subtract(height, step))
    {
        // Push top 10 indexes first, then back off exponentially.
        if (heights.size() >= 10)
            step <<= 1;

        heights.push_back(height);
    }

    // Push the genesis block index.
    heights.push_back(0);

    // Validate the reservation computation.
    BITCOIN_ASSERT(heights.size() <= reservation);
    return heights;
}

// Utilities.
//-----------------------------------------------------------------------------

// Clear witness from all inputs (does not change default transaction hash).
void block::strip_witness()
{
    const auto strip = [](transaction& transaction)
    {
        transaction.strip_witness();
    };

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    unique_lock lock(mutex_);

    segregated_ = false;
    total_size_ = boost::none;
    std::for_each(transactions_.begin(), transactions_.end(), strip);
    ///////////////////////////////////////////////////////////////////////////
}

// Validation helpers.
//-----------------------------------------------------------------------------

// static
uint256_t block::proof(uint32_t bits)
{
    const auto header_bits = compact(bits);

    if (header_bits.is_overflowed())
        return 0;

    uint256_t target(header_bits);

    //*************************************************************************
    // CONSENSUS: satoshi will throw division by zero in the case where the
    // target is (2^256)-1 as the overflow will result in a zero divisor.
    // While actually achieving this work is improbable, this method operates
    // on user data method and therefore must be guarded.
    //*************************************************************************
    const auto divisor = target + 1;

    // We need to compute 2**256 / (target + 1), but we can't represent 2**256
    // as it's too large for uint256. However as 2**256 is at least as large as
    // target + 1, it is equal to ((2**256 - target - 1) / (target + 1)) + 1, or
    // (~target / (target + 1)) + 1.
    return (divisor == 0) ? 0 : (~target / divisor) + 1;
}

// [GetBlockProof]
uint256_t block::proof() const
{
    return proof(header_.bits());
}

uint64_t block::subsidy(size_t height, bool retarget)
{
    static const auto overflow = sizeof(uint64_t) * byte_bits;
    auto subsidy = initial_block_subsidy_satoshi();
    const auto halvings = height / subsidy_interval(retarget);
    subsidy >>= (halvings >= overflow ? 0 : halvings);
    return subsidy;
}

// Returns max_size_t in case of overflow or unpopulated chain state.
size_t block::signature_operations() const
{
    const auto state = validation.state;
    const auto bip16 = state->is_enabled(rule_fork::bip16_rule);
    const auto bip141 = state->is_enabled(rule_fork::bip141_rule);
    return state ? signature_operations(bip16, bip141) : max_size_t;
}

// Returns max_size_t in case of overflow.
size_t block::signature_operations(bool bip16, bool bip141) const
{
    const auto value = [bip16, bip141](size_t total, const transaction& tx)
    {
        return ceiling_add(total, tx.signature_operations(bip16, bip141));
    };

    //*************************************************************************
    // CONSENSUS: Legacy sigops are counted in coinbase scripts despite the
    // fact that coinbase input scripts are never executed. There is no need
    // to exclude p2sh coinbase sigops since there is never a script to count.
    //*************************************************************************
    const auto& txs = transactions_;
    return std::accumulate(txs.begin(), txs.end(), size_t{0}, value);
}

size_t block::total_inputs(bool with_coinbase) const
{
    size_t value;

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if (total_inputs_ != boost::none)
    {
        value = total_inputs_.get();
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return value;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    const auto inputs = [](size_t total, const transaction& tx)
    {
        return safe_add(total, tx.inputs().size());
    };

    const auto& txs = transactions_;
    const size_t offset = with_coinbase ? 0 : 1;
    value = std::accumulate(txs.begin() + offset, txs.end(), size_t(0), inputs);
    total_inputs_ = value;
    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    return value;
}

size_t block::weight() const
{
    // Block weight is 3 * Base size * + 1 * Total size (bip141).
    return base_size_contribution * serialized_size(false) +
        total_size_contribution * serialized_size(true);
}

// True if there is another coinbase other than the first tx.
// No txs or coinbases returns false.
bool block::is_extra_coinbases() const
{
    if (transactions_.empty())
        return false;

    const auto value = [](const transaction& tx)
    {
        return tx.is_coinbase();
    };

    const auto& txs = transactions_;
    return std::any_of(txs.begin() + 1, txs.end(), value);
}

bool block::is_final(size_t height, uint32_t block_time) const
{
    const auto value = [=](const transaction& tx)
    {
        return tx.is_final(height, block_time);
    };

    const auto& txs = transactions_;
    return std::all_of(txs.begin(), txs.end(), value);
}

// Distinctness is defined by transaction hash.
bool block::is_distinct_transaction_set() const
{
    const auto hasher = [](const transaction& tx) { return tx.hash(); };
    const auto& txs = transactions_;
    hash_list hashes(txs.size());
    std::transform(txs.begin(), txs.end(), hashes.begin(), hasher);
    std::sort(hashes.begin(), hashes.end());
    const auto distinct_end = std::unique(hashes.begin(), hashes.end());
    return distinct_end == hashes.end();
}

hash_digest block::generate_merkle_root(bool witness) const
{
    if (transactions_.empty())
        return null_hash;

    hash_list update;
    auto merkle = to_hashes(witness);

    // Initial capacity is half of the original list (clear doesn't reset).
    update.reserve((merkle.size() + 1) / 2);

    while (merkle.size() > 1)
    {
        // If number of hashes is odd, duplicate last hash in the list.
        if (merkle.size() % 2 != 0)
            merkle.push_back(merkle.back());

        for (auto it = merkle.begin(); it != merkle.end(); it += 2)
            update.push_back(bitcoin_hash(build_chunk({ it[0], it[1] })));

        std::swap(merkle, update);
        update.clear();
    }

    // There is now only one item in the list.
    return merkle.front();
}

size_t block::non_coinbase_input_count() const
{
    if (transactions_.empty())
        return 0;

    const auto counter = [](size_t sum, const transaction& tx)
    {
        return sum + tx.inputs().size();
    };

    const auto& txs = transactions_;
    return std::accumulate(txs.begin() + 1, txs.end(), size_t(0), counter);
}

//****************************************************************************
// CONSENSUS: This is only necessary because satoshi stores and queries as it
// validates, imposing an otherwise unnecessary partial transaction ordering.
//*****************************************************************************
bool block::is_forward_reference() const
{
    std::unordered_map<hash_digest, bool> hashes(transactions_.size());
    const auto is_forward = [&hashes](const input& input)
    {
        return hashes.count(input.previous_output().hash()) != 0;
    };

    for (const auto& tx: reverse(transactions_))
    {
        hashes.emplace(tx.hash(), true);

        if (std::any_of(tx.inputs().begin(), tx.inputs().end(), is_forward))
            return true;
    }

    return false;
}

// This is an early check that is redundant with block pool accept checks.
bool block::is_internal_double_spend() const
{
    if (transactions_.empty())
        return false;

    point::list outs;
    outs.reserve(non_coinbase_input_count());
    const auto& txs = transactions_;

    // Merge the prevouts of all non-coinbase transactions into one set.
    for (auto tx = txs.begin() + 1; tx != txs.end(); ++tx)
    {
        auto out = tx->previous_outputs();
        std::move(out.begin(), out.end(), std::inserter(outs, outs.end()));
    }

    std::sort(outs.begin(), outs.end());
    const auto distinct_end = std::unique(outs.begin(), outs.end());
    const auto distinct = (distinct_end == outs.end());
    return !distinct;
}

bool block::is_valid_merkle_root() const
{
    return generate_merkle_root() == header_.merkle();
}

// Overflow returns max_uint64.
uint64_t block::fees() const
{
    ////static_assert(max_money() < max_uint64, "overflow sentinel invalid");
    const auto value = [](uint64_t total, const transaction& tx)
    {
        return ceiling_add(total, tx.fees());
    };

    const auto& txs = transactions_;
    return std::accumulate(txs.begin(), txs.end(), uint64_t{0}, value);
}

uint64_t block::claim() const
{
    return transactions_.empty() ? 0 :
        transactions_.front().total_output_value();
}

// Overflow returns max_uint64.
uint64_t block::reward(size_t height) const
{
    ////static_assert(max_money() < max_uint64, "overflow sentinel invalid");
    return ceiling_add(fees(), subsidy(height));
}

bool block::is_valid_coinbase_claim(size_t height) const
{
    return claim() <= reward(height);
}

bool block::is_valid_coinbase_script(size_t height) const
{
    if (transactions_.empty() || transactions_.front().inputs().empty())
        return false;

    const auto& script = transactions_.front().inputs().front().script();
    return script::is_coinbase_pattern(script.operations(), height);
}

bool block::is_valid_witness_commitment() const
{
    if (transactions_.empty() || transactions_.front().inputs().empty())
        return false;

    hash_digest reserved, committed;
    const auto& coinbase = transactions_.front();

    // Last output of commitment pattern holds committed value (bip141).
    if (coinbase.inputs().front().extract_reserved_hash(reserved))
        for (const auto& output: reverse(coinbase.outputs()))
            if (output.extract_committed_hash(committed))
                return committed == bitcoin_hash(
                    build_chunk({ generate_merkle_root(true), reserved }));

    // If no txs in block are segregated the commitment is optional (bip141).
    return !is_segregated();
}

bool block::is_segregated() const
{
    bool value;

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if (segregated_ != boost::none)
    {
        value = segregated_.get();
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return value;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    const auto segregated = [](const transaction& tx)
    {
        return tx.is_segregated();
    };

    // If no block tx has witness data the commitment is optional (bip141).
    value = std::any_of(transactions_.begin(), transactions_.end(), segregated);

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    return value;
}

code block::check_transactions() const
{
    code ec;

    for (const auto& tx: transactions_)
        if ((ec = tx.check(false)))
            return ec;

    return error::success;
}

code block::accept_transactions(const chain_state& state) const
{
    code ec;

    for (const auto& tx: transactions_)
        if ((ec = tx.accept(state, false)))
            return ec;

    return error::success;
}

code block::connect_transactions(const chain_state& state) const
{
    code ec;

    for (const auto& tx: transactions_)
        if ((ec = tx.connect(state)))
            return ec;

    return error::success;
}

// Validation.
//-----------------------------------------------------------------------------

// These checks are self-contained; blockchain (and so version) independent.
code block::check() const
{
    validation.start_check = asio::steady_clock::now();

    code ec;

    if ((ec = header_.check()))
        return ec;

    // TODO: relates to total of tx.size(false) (pool cache).
    else if (serialized_size(false) > max_block_size)
        return error::block_size_limit;

    else if (transactions_.empty())
        return error::empty_block;

    else if (!transactions_.front().is_coinbase())
        return error::first_not_coinbase;

    else if (is_extra_coinbases())
        return error::extra_coinbases;

    // TODO: determinable from tx pool graph.
    else if (is_forward_reference())
        return error::forward_reference;

    // This is subset of is_internal_double_spend if collisions cannot happen.
    ////else if (!is_distinct_transaction_set())
    ////    return error::internal_duplicate;

    // TODO: determinable from tx pool graph.
    else if (is_internal_double_spend())
        return error::block_internal_double_spend;

    // TODO: relates height to tx.hash(false) (pool cache).
    else if (!is_valid_merkle_root())
        return error::merkle_mismatch;

    // We cannot know if bip16 is enabled at this point so we disable it.
    // This will not make a difference unless prevouts are populated, in which
    // case they are ignored. This means that p2sh sigops are not counted here.
    // This is a preliminary check, the final count must come from connect().
    ////else if (signature_operations(false, false) > max_block_sigops)
    ////    return error::block_legacy_sigop_limit;

    else
        return check_transactions();
}

code block::accept(bool transactions) const
{
    const auto state = validation.state;
    return state ? accept(*state, transactions) : error::operation_failed;
}

// These checks assume that prevout caching is completed on all tx.inputs.
code block::accept(const chain_state& state, bool transactions) const
{
    validation.start_accept = asio::steady_clock::now();

    code ec;
    const auto bip16 = state.is_enabled(rule_fork::bip16_rule);
    const auto bip34 = state.is_enabled(rule_fork::bip34_rule);
    const auto bip113 = state.is_enabled(rule_fork::bip113_rule);
    const auto bip141 = state.is_enabled(rule_fork::bip141_rule);

    const auto max_sigops = bip141 ? max_fast_sigops : max_block_sigops;
    const auto block_time = bip113 ? state.median_time_past() :
        header_.timestamp();

    if ((ec = header_.accept(state)))
        return ec;

    else if (state.is_under_checkpoint())
        return error::success;

    // TODO: relates height to total of tx.size(true) (pool cache).
    else if (bip141 && weight() > max_block_weight)
        return error::block_weight_limit;

    else if (bip34 && !is_valid_coinbase_script(state.height()))
        return error::coinbase_height_mismatch;

    // TODO: relates height to total of tx.fee (pool cach).
    else if (!is_valid_coinbase_claim(state.height()))
        return error::coinbase_value_limit;

    // TODO: relates median time past to tx.locktime (pool cache min tx.time).
    else if (!is_final(state.height(), block_time))
        return error::block_non_final;

    // TODO: relates height to tx.hash(true) (pool cache).
    else if (bip141 && !is_valid_witness_commitment())
        return error::invalid_witness_commitment;

    // TODO: determine if performance benefit is worth excluding sigops here.
    // TODO: relates block limit to total of tx.sigops (pool cache).
    else if (transactions && signature_operations(bip16, bip141) > max_sigops)
        return error::block_embedded_sigop_limit;

    else if (transactions)
        return accept_transactions(state);

    else
        return ec;
}

code block::connect() const
{
    const auto state = validation.state;
    return state ? connect(*state) : error::operation_failed;
}

code block::connect(const chain_state& state) const
{
    validation.start_connect = asio::steady_clock::now();

    if (state.is_under_checkpoint())
        return error::success;

    else
        return connect_transactions(state);
}

} // namespace chain
} // namespace libbitcoin
