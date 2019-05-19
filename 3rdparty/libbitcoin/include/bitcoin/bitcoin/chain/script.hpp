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
#ifndef LIBBITCOIN_CHAIN_SCRIPT_HPP
#define LIBBITCOIN_CHAIN_SCRIPT_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <bitcoin/bitcoin/constants.hpp>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/error.hpp>
#include <bitcoin/bitcoin/math/elliptic_curve.hpp>
#include <bitcoin/bitcoin/machine/operation.hpp>
#include <bitcoin/bitcoin/machine/rule_fork.hpp>
#include <bitcoin/bitcoin/machine/script_pattern.hpp>
#include <bitcoin/bitcoin/machine/script_version.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>
#include <bitcoin/bitcoin/utility/reader.hpp>
#include <bitcoin/bitcoin/utility/thread.hpp>
#include <bitcoin/bitcoin/utility/writer.hpp>

namespace libbitcoin {
namespace chain {

class transaction;
class witness;

class BC_API script
{
public:
    typedef machine::operation operation;
    typedef machine::rule_fork rule_fork;
    typedef machine::script_pattern script_pattern;
    typedef machine::script_version script_version;

    // Constructors.
    //-------------------------------------------------------------------------

    script();

    script(script&& other);
    script(const script& other);

    script(operation::list&& ops);
    script(const operation::list& ops);

    script(data_chunk&& encoded, bool prefix);
    script(const data_chunk& encoded, bool prefix);

    // Operators.
    //-------------------------------------------------------------------------

    /// This class is move assignable and copy assignable.
    script& operator=(script&& other);
    script& operator=(const script& other);

    bool operator==(const script& other) const;
    bool operator!=(const script& other) const;

    // Deserialization.
    //-------------------------------------------------------------------------

    static script factory_from_data(const data_chunk& encoded, bool prefix);
    static script factory_from_data(std::istream& stream, bool prefix);
    static script factory_from_data(reader& source, bool prefix);

    /// Deserialization invalidates the iterator.
    bool from_data(const data_chunk& encoded, bool prefix);
    bool from_data(std::istream& stream, bool prefix);
    bool from_data(reader& source, bool prefix);

    /// Deserialization invalidates the iterator.
    void from_operations(operation::list&& ops);
    void from_operations(const operation::list& ops);
    bool from_string(const std::string& mnemonic);

    /// A script object is valid if the byte count matches the prefix.
    bool is_valid() const;

    /// Script operations is valid if all push ops have the predicated size.
    bool is_valid_operations() const;

    // Serialization.
    //-------------------------------------------------------------------------

    data_chunk to_data(bool prefix) const;
    void to_data(std::ostream& stream, bool prefix) const;
    void to_data(writer& sink, bool prefix) const;

    std::string to_string(uint32_t active_forks) const;

    // Iteration.
    //-------------------------------------------------------------------------

    void clear();
    bool empty() const;
    size_t size() const;
    const operation& front() const;
    const operation& back() const;
    operation::iterator begin() const;
    operation::iterator end() const;
    const operation& operator[](size_t index) const;

    // Properties (size, accessors, cache).
    //-------------------------------------------------------------------------

    size_t serialized_size(bool prefix) const;
    const operation::list& operations() const;

    // Signing.
    //-------------------------------------------------------------------------

    static hash_digest generate_signature_hash(const transaction& tx,
        uint32_t input_index, const script& script_code, uint8_t sighash_type,
        script_version version=script_version::unversioned,
        uint64_t value=max_uint64);

    static bool check_signature(const ec_signature& signature,
        uint8_t sighash_type, const data_chunk& public_key,
        const script& script_code, const transaction& tx, uint32_t input_index,
        script_version version=script_version::unversioned,
        uint64_t value=max_uint64);

    static bool create_endorsement(endorsement& out, const ec_secret& secret,
        const script& prevout_script, const transaction& tx,
        uint32_t input_index, uint8_t sighash_type,
        script_version version=script_version::unversioned,
        uint64_t value=max_uint64);

    // Utilities (static).
    //-------------------------------------------------------------------------

    /// Transaction helpers.
    static hash_digest to_outputs(const transaction& tx);
    static hash_digest to_inpoints(const transaction& tx);
    static hash_digest to_sequences(const transaction& tx);

    /// Determine if the fork is enabled in the active forks set.
    static bool is_enabled(uint32_t active_forks, rule_fork fork)
    {
        return (fork & active_forks) != 0;
    }

    /// Consensus patterns.
    static bool is_push_only(const operation::list& ops);
    static bool is_relaxed_push(const operation::list& ops);
    static bool is_coinbase_pattern(const operation::list& ops, size_t height);
    static bool is_commitment_pattern(const operation::list& ops);
    static bool is_witness_program_pattern(const operation::list& ops);

    /// Common output patterns (psh and pwsh are also consensus).
    static bool is_null_data_pattern(const operation::list& ops);
    static bool is_pay_multisig_pattern(const operation::list& ops);
    static bool is_pay_public_key_pattern(const operation::list& ops);
    static bool is_pay_key_hash_pattern(const operation::list& ops);
    static bool is_pay_script_hash_pattern(const operation::list& ops);
    static bool is_pay_witness_script_hash_pattern(const operation::list& ops);

    /// Common input patterns (skh is also consensus).
    static bool is_sign_multisig_pattern(const operation::list& ops);
    static bool is_sign_public_key_pattern(const operation::list& ops);
    static bool is_sign_key_hash_pattern(const operation::list& ops);
    static bool is_sign_script_hash_pattern(const operation::list& ops);

    /// Stack factories.
    static operation::list to_null_data_pattern(data_slice data);
    static operation::list to_pay_public_key_pattern(data_slice point);
    static operation::list to_pay_key_hash_pattern(const short_hash& hash);
    static operation::list to_pay_script_hash_pattern(const short_hash& hash);
    static operation::list to_pay_multisig_pattern(uint8_t signatures,
        const point_list& points);
    static operation::list to_pay_multisig_pattern(uint8_t signatures,
        const data_stack& points);

    // Utilities (non-static).
    //-------------------------------------------------------------------------

    /// Common pattern detection.
    data_chunk witness_program() const;
    script_version version() const;
    script_pattern pattern() const;
    script_pattern output_pattern() const;
    script_pattern input_pattern() const;

    /// Consensus computations.
    size_t sigops(bool accurate) const;
    void find_and_delete(const data_stack& endorsements);
    bool is_unspendable() const;

    // Validation.
    //-------------------------------------------------------------------------

    static code verify(const transaction& tx, uint32_t input, uint32_t forks);

    // TODO: move back to private.
    static code verify(const transaction& tx, uint32_t input_index,
        uint32_t forks, const script& input_script,
        const witness& input_witness, const script& prevout_script,
        uint64_t value);

protected:
    // So that input and output may call reset from their own.
    friend class input;
    friend class output;

    void reset();
    bool is_pay_to_witness(uint32_t forks) const;
    bool is_pay_to_script_hash(uint32_t forks) const;

private:
    static size_t serialized_size(const operation::list& ops);
    static data_chunk operations_to_data(const operation::list& ops);
    static hash_digest generate_unversioned_signature_hash(
        const transaction& tx, uint32_t input_index,
        const script& script_code, uint8_t sighash_type);
    static hash_digest generate_version_0_signature_hash(const transaction& tx,
        uint32_t input_index, const script& script_code, uint64_t value,
        uint8_t sighash_type);

    void find_and_delete_(const data_chunk& endorsement);

    data_chunk bytes_;
    bool valid_;

    // These are protected by mutex.
    mutable bool cached_;
    mutable operation::list operations_;
    mutable upgrade_mutex mutex_;
};

} // namespace chain
} // namespace libbitcoin

#endif
