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
#include <bitcoin/bitcoin/math/elliptic_curve.hpp>

#include <algorithm>
#include <utility>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <bitcoin/bitcoin/math/hash.hpp>
#include <bitcoin/bitcoin/math/limits.hpp>
#include <bitcoin/bitcoin/utility/assert.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>
#include "../math/external/lax_der_parsing.h"
#include "secp256k1_initializer.hpp"

namespace libbitcoin {

static constexpr uint8_t compressed_even = 0x02;
static constexpr uint8_t compressed_odd = 0x03;
static constexpr uint8_t uncompressed = 0x04;

BC_CONSTFUNC int to_flags(bool compressed)
{
    return compressed ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED;
}

// Helper templates
// ----------------------------------------------------------------------------
// These allow strong typing of private keys without redundant code.

template <size_t Size>
bool parse(const secp256k1_context* context, secp256k1_pubkey& out,
    const byte_array<Size>& point)
{
    return secp256k1_ec_pubkey_parse(context, &out, point.data(), Size) == 1;
}

template <size_t Size>
bool serialize(const secp256k1_context* context, byte_array<Size>& out,
    const secp256k1_pubkey point)
{
    auto size = Size;
    const auto flags = to_flags(Size == ec_compressed_size);
    secp256k1_ec_pubkey_serialize(context, out.data(), &size, &point, flags);
    return size == Size;
}

template <size_t Size>
bool ec_add(const secp256k1_context* context, byte_array<Size>& in_out,
    const ec_secret& secret)
{
    secp256k1_pubkey pubkey;
    return parse(context, pubkey, in_out) &&
        secp256k1_ec_pubkey_tweak_add(context, &pubkey, secret.data()) == 1 &&
        serialize(context, in_out, pubkey);
}

template <size_t Size>
bool ec_multiply(const secp256k1_context* context, byte_array<Size>& in_out,
    const ec_secret& secret)
{
    secp256k1_pubkey pubkey;
    return parse(context, pubkey, in_out) &&
        secp256k1_ec_pubkey_tweak_mul(context, &pubkey, secret.data()) == 1 &&
        serialize(context, in_out, pubkey);
}

template <size_t Size>
bool secret_to_public(const secp256k1_context* context, byte_array<Size>& out,
    const ec_secret& secret)
{
    secp256k1_pubkey pubkey;
    return secp256k1_ec_pubkey_create(context, &pubkey, secret.data()) == 1 &&
        serialize(context, out, pubkey);
}

template <size_t Size>
bool recover_public(const secp256k1_context* context, byte_array<Size>& out,
    const recoverable_signature& recoverable, const hash_digest& hash)
{
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_recoverable_signature sign;
    const auto recovery_id = safe_to_signed<int>(recoverable.recovery_id);
    return
        secp256k1_ecdsa_recoverable_signature_parse_compact(context,
            &sign, recoverable.signature.data(), recovery_id) == 1 &&
        secp256k1_ecdsa_recover(context, &pubkey, &sign, hash.data()) == 1 &&
            serialize(context, out, pubkey);
}

bool verify_signature(const secp256k1_context* context,
    const secp256k1_pubkey point, const hash_digest& hash,
    const ec_signature& signature)
{
    // Copy to avoid exposing external types.
    secp256k1_ecdsa_signature parsed;
    std::copy_n(signature.begin(), ec_signature_size, std::begin(parsed.data));

    // secp256k1_ecdsa_verify rejects non-normalized (low-s) signatures, but
    // bitcoin does not have such a limitation, so we always normalize.
    secp256k1_ecdsa_signature normal;
    secp256k1_ecdsa_signature_normalize(context, &normal, &parsed);
    return secp256k1_ecdsa_verify(context, &normal, hash.data(), &point) == 1;
}

// Add and multiply EC values
// ----------------------------------------------------------------------------

bool ec_add(ec_compressed& point, const ec_secret& secret)
{
    const auto context = verification.context();
    return ec_add(context, point, secret);
}

bool ec_add(ec_uncompressed& point, const ec_secret& secret)
{
    const auto context = verification.context();
    return ec_add(context, point, secret);
}

bool ec_add(ec_secret& left, const ec_secret& right)
{
    const auto context = verification.context();
    return secp256k1_ec_privkey_tweak_add(context, left.data(),
        right.data()) == 1;
}

bool ec_multiply(ec_compressed& point, const ec_secret& secret)
{
    const auto context = verification.context();
    return ec_multiply(context, point, secret);
}

bool ec_multiply(ec_uncompressed& point, const ec_secret& secret)
{
    const auto context = verification.context();
    return ec_multiply(context, point, secret);
}

bool ec_multiply(ec_secret& left, const ec_secret& right)
{
    const auto context = verification.context();
    return secp256k1_ec_privkey_tweak_mul(context, left.data(),
        right.data()) == 1;
}

// Convert keys
// ----------------------------------------------------------------------------

bool compress(ec_compressed& out, const ec_uncompressed& point)
{
    secp256k1_pubkey pubkey;
    const auto context = verification.context();
    return parse(context, pubkey, point) && serialize(context, out, pubkey);
}

bool decompress(ec_uncompressed& out, const ec_compressed& point)
{
    secp256k1_pubkey pubkey;
    const auto context = verification.context();
    return parse(context, pubkey, point) && serialize(context, out, pubkey);
}

bool secret_to_public(ec_compressed& out, const ec_secret& secret)
{
    const auto context = signing.context();
    return secret_to_public(context, out, secret);
}

bool secret_to_public(ec_uncompressed& out, const ec_secret& secret)
{
    const auto context = signing.context();
    return secret_to_public(context, out, secret);
}

// Verify keys
// ----------------------------------------------------------------------------

bool verify(const ec_secret& secret)
{
    const auto context = verification.context();
    return secp256k1_ec_seckey_verify(context, secret.data()) == 1;
}

bool verify(const ec_compressed& point)
{
    secp256k1_pubkey pubkey;
    const auto context = verification.context();
    return parse(context, pubkey, point);
}

bool verify(const ec_uncompressed& point)
{
    secp256k1_pubkey pubkey;
    const auto context = verification.context();
    return parse(context, pubkey, point);
}

// Detect public keys
// ----------------------------------------------------------------------------

bool is_compressed_key(data_slice point)
{
    const auto size = point.size();
    if (size != ec_compressed_size)
        return false;

    const auto first = point.data()[0];
    return first == compressed_even || first == compressed_odd;
}

bool is_uncompressed_key(data_slice point)
{
    const auto size = point.size();
    if (size != ec_uncompressed_size)
        return false;

    const auto first = point.data()[0];
    return first == uncompressed;
}

bool is_public_key(data_slice point)
{
    return is_compressed_key(point) || is_uncompressed_key(point);
}

bool is_even_key(const ec_compressed& point)
{
    return point.front() == ec_even_sign;
}

bool is_endorsement(const endorsement& endorsement)
{
    const auto size = endorsement.size();
    return size >= min_endorsement_size && size <= max_endorsement_size;
}

// DER parse/encode
// ----------------------------------------------------------------------------

bool parse_endorsement(uint8_t& sighash_type, der_signature& der_signature,
    endorsement&& endorsement)
{
    if (endorsement.empty())
        return false;

    sighash_type = endorsement.back();
    endorsement.pop_back();
    der_signature = std::move(endorsement);
    return true;
}

bool parse_signature(ec_signature& out, const der_signature& der_signature,
    bool strict)
{
    if (der_signature.empty())
        return false;

    bool valid;
    secp256k1_ecdsa_signature parsed;
    const auto context = verification.context();

    if (strict)
        valid = secp256k1_ecdsa_signature_parse_der(context, &parsed,
            der_signature.data(), der_signature.size()) == 1;
    else
        valid = ecdsa_signature_parse_der_lax(context, &parsed,
            der_signature.data(), der_signature.size()) == 1;

    if (valid)
        std::copy_n(std::begin(parsed.data), ec_signature_size, out.begin());

    return valid;
}

bool encode_signature(der_signature& out, const ec_signature& signature)
{
    // Copy to avoid exposing external types.
    secp256k1_ecdsa_signature sign;
    std::copy_n(signature.begin(), ec_signature_size, std::begin(sign.data));

    const auto context = signing.context();
    auto size = max_der_signature_size;
    out.resize(size);

    if (secp256k1_ecdsa_signature_serialize_der(context, out.data(), &size,
        &sign) != 1)
        return false;

    out.resize(size);
    return true;
}

// EC sign/verify
// ----------------------------------------------------------------------------

bool sign(ec_signature& out, const ec_secret& secret, const hash_digest& hash)
{
    secp256k1_ecdsa_signature signature;
    const auto context = signing.context();

    if (secp256k1_ecdsa_sign(context, &signature, hash.data(), secret.data(),
        secp256k1_nonce_function_rfc6979, nullptr) != 1)
        return false;

    std::copy_n(std::begin(signature.data), out.size(), out.begin());
    return true;
}

bool verify_signature(const ec_compressed& point, const hash_digest& hash,
    const ec_signature& signature)
{
    secp256k1_pubkey pubkey;
    const auto context = verification.context();
    return parse(context, pubkey, point) &&
        verify_signature(context, pubkey, hash, signature);
}

bool verify_signature(const ec_uncompressed& point, const hash_digest& hash,
    const ec_signature& signature)
{
    secp256k1_pubkey pubkey;
    const auto context = verification.context();
    return parse(context, pubkey, point) &&
        verify_signature(context, pubkey, hash, signature);
}

bool verify_signature(data_slice point, const hash_digest& hash,
    const ec_signature& signature)
{
    // Copy to avoid exposing external types.
    secp256k1_ecdsa_signature parsed;
    std::copy_n(signature.begin(), ec_signature_size, std::begin(parsed.data));

    // secp256k1_ecdsa_verify rejects non-normalized (low-s) signatures, but
    // bitcoin does not have such a limitation, so we always normalize.
    secp256k1_ecdsa_signature normal;
    const auto context = verification.context();
    secp256k1_ecdsa_signature_normalize(context, &normal, &parsed);

    // This uses a data slice and calls secp256k1_ec_pubkey_parse() in place of
    // parse() so that we can support the der_verify data_chunk optimization.
    secp256k1_pubkey pubkey;
    const auto size = point.size();
    return
        secp256k1_ec_pubkey_parse(context, &pubkey, point.data(), size) == 1 &&
        secp256k1_ecdsa_verify(context, &normal, hash.data(), &pubkey) == 1;
}

// Recoverable sign/recover
// ----------------------------------------------------------------------------

bool sign_recoverable(recoverable_signature& out, const ec_secret& secret,
    const hash_digest& hash)
{
    int recovery_id;
    const auto context = signing.context();
    secp256k1_ecdsa_recoverable_signature signature;

    const auto result =
        secp256k1_ecdsa_sign_recoverable(context, &signature, hash.data(),
            secret.data(), secp256k1_nonce_function_rfc6979, nullptr) == 1 &&
        secp256k1_ecdsa_recoverable_signature_serialize_compact(context,
            out.signature.data(), &recovery_id, &signature) == 1;

    BITCOIN_ASSERT(recovery_id >= 0 && recovery_id <= 3);
    out.recovery_id = safe_to_unsigned<uint8_t>(recovery_id);
    return result;
}

bool recover_public(ec_compressed& out,
    const recoverable_signature& recoverable, const hash_digest& hash)
{
    const auto context = verification.context();
    return recover_public(context, out, recoverable, hash);
}

bool recover_public(ec_uncompressed& out,
    const recoverable_signature& recoverable, const hash_digest& hash)
{
    const auto context = verification.context();
    return recover_public(context, out, recoverable, hash);
}

} // namespace libbitcoin
