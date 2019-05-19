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
#ifndef LIBBITCOIN_CHAIN_CHAIN_STATE_HPP
#define LIBBITCOIN_CHAIN_CHAIN_STATE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <deque>
#include <bitcoin/bitcoin/config/checkpoint.hpp>
#include <bitcoin/bitcoin/constants.hpp>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/math/hash.hpp>
#include <bitcoin/bitcoin/machine/opcode.hpp>
#include <bitcoin/bitcoin/machine/rule_fork.hpp>

namespace libbitcoin {
namespace chain {

class block;
class header;

class BC_API chain_state
{
public:
    typedef std::deque<uint32_t> bitss;
    typedef std::deque<uint32_t> versions;
    typedef std::deque<uint32_t> timestamps;
    typedef struct { size_t count; size_t high; } range;

    typedef std::shared_ptr<chain_state> ptr;
    typedef config::checkpoint::list checkpoints;

    /// Heights used to identify construction requirements.
    /// All values are lower-bounded by the genesis block height.
    /// Obtaining all values even in the case where the set of queries could be
    /// short-circuited allows us to cache results for the next block,
    /// minimizing overall querying.
    struct map
    {
        // This sentinel indicates that the value was not requested.
        static const size_t unrequested = max_size_t;

        /// [block - 1, floor(block - 2016, 0)] mainnet: 1, testnet: 2016|0
        range bits;

        /// (block - 0), used only for populating bits.ordered on increment.
        size_t bits_self;

        /// [block - 1, floor(block - 1000, 0)] mainnet: 1000, testnet: 100
        range version;

        /// (block - 0)
        size_t version_self;

        /// [block - 1, floor(block - 11, 0)]
        range timestamp;

        /// (block - 0)
        size_t timestamp_self;

        /// (block - (block % 2016 == 0 ? 2016 : block % 2016))
        size_t timestamp_retarget;

        /// mainnet: 227931, testnet: 21111 (or map::unrequested)
        size_t allow_collisions_height;

        /// mainnet: 419328, testnet: 770112 (or map::unrequested)
        size_t bip9_bit0_height;

        /// mainnet: 481824, testnet: 834624 (or map::unrequested)
        size_t bip9_bit1_height;
    };

    /// Values used to populate chain state at the target height.
    struct data
    {
        /// Header values are based on this height.
        size_t height;

        /// Hash of the candidate block or null_hash for memory pool.
        hash_digest hash;

        /// Hash of the allow_collisions block or null_hash if unrequested.
        hash_digest allow_collisions_hash;

        /// Hash of the bip9_bit0 block or null_hash if unrequested.
        hash_digest bip9_bit0_hash;

        /// Hash of the bip9_bit1 block or null_hash if unrequested.
        hash_digest bip9_bit1_hash;

        /// Values must be ordered by height with high (block - 1) last.
        struct
        {
            uint32_t self;
            bitss ordered;
        } bits;

        /// Values must be ordered by height with high (block - 1) last.
        struct
        {
            uint32_t self;
            versions ordered;
        } version;

        /// Values must be ordered by height with high (block - 1) last.
        struct
        {
            uint32_t self;
            uint32_t retarget;
            timestamps ordered;
        } timestamp;
    };

    /// Checkpoints must be ordered by height with greatest at back.
    static map get_map(size_t height, const checkpoints& checkpoints,
        uint32_t forks);

    static uint32_t signal_version(uint32_t forks);

    /// Create pool state from top chain top block state.
    chain_state(const chain_state& top);

    /// Create block state from tx pool chain state of same height.
    chain_state(const chain_state& pool, const chain::block& block);

    /// Create header state from header pool chain state of previous height.
    chain_state(const chain_state& parent, const chain::header& header);

    /// Checkpoints must be ordered by height with greatest at back.
    /// Forks and checkpoints must match those provided for map creation.
    chain_state(data&& values, const checkpoints& checkpoints, uint32_t forks);

    /// Properties.
    size_t height() const;
    uint32_t enabled_forks() const;
    uint32_t minimum_version() const;
    uint32_t median_time_past() const;
    uint32_t work_required() const;

    /// Construction with zero height or any empty array causes invalid state.
    bool is_valid() const;

    /// Determine if the fork is set for this block.
    bool is_enabled(machine::rule_fork fork) const;

    /// Determine if this block hash fails a checkpoint at this height.
    bool is_checkpoint_conflict(const hash_digest& hash) const;

    /// This block height is less than or equal to that of the top checkpoint.
    bool is_under_checkpoint() const;

protected:
    struct activations
    {
        // The forks that are active at this height.
        uint32_t forks;

        // The minimum block version required at this height.
        uint32_t minimum_version;
    };

    static activations activation(const data& values, uint32_t forks);
    static uint32_t median_time_past(const data& values, uint32_t forks);
    static uint32_t work_required(const data& values, uint32_t forks);

private:
    static size_t bits_count(size_t height, uint32_t forks);
    static size_t version_count(size_t height, uint32_t forks);
    static size_t timestamp_count(size_t height, uint32_t forks);
    static size_t retarget_height(size_t height, uint32_t forks);
    static size_t collision_height(size_t height, uint32_t forks);
    static size_t bip9_bit0_height(size_t height, uint32_t forks);
    static size_t bip9_bit1_height(size_t height, uint32_t forks);

    static data to_pool(const chain_state& top);
    static data to_block(const chain_state& pool, const block& block);
    static data to_header(const chain_state& parent, const header& header);

    static uint32_t work_required_retarget(const data& values);
    static uint32_t retarget_timespan(const chain_state::data& values);

    // easy blocks
    static uint32_t easy_work_required(const data& values);
    static uint32_t easy_time_limit(const chain_state::data& values);
    static bool is_retarget_or_non_limit(size_t height, uint32_t bits);
    static bool is_retarget_height(size_t height);
    static size_t retarget_distance(size_t height);

    // This is retained as an optimization for other constructions.
    // A similar height clone can be partially computed, reducing query cost.
    const data data_;

    // Configured forks are saved for state transitions.
    const uint32_t forks_;

    // Checkpoints do not affect the data that is collected or promoted.
    const config::checkpoint::list& checkpoints_;

    // These are computed on construct from sample and checkpoints.
    const activations active_;
    const uint32_t median_time_past_;
    const uint32_t work_required_;
};

} // namespace chain
} // namespace libbitcoin

#endif
