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
#include "parse_encrypted_token.hpp"

#include <cstdint>
#include <cstddef>
#include <bitcoin/bitcoin/math/checksum.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>
#include <bitcoin/bitcoin/wallet/encrypted_keys.hpp>
#include "parse_encrypted_prefix.hpp"

namespace libbitcoin {
namespace wallet {

// This prefix results in the prefix "passphrase" in the base58 encoding.
// The prefix is not modified as the result of variations to address.
const byte_array<parse_encrypted_token::magic_size> parse_encrypted_token::magic_
{
    { 0x2c, 0xe9, 0xb3, 0xe1, 0xff, 0x39, 0xe2 }
};

byte_array<parse_encrypted_token::prefix_size>
parse_encrypted_token::prefix_factory(bool lot_sequence)
{
    const auto context = lot_sequence ? lot_context_ : default_context_;
    return splice(magic_, to_array(context));
}

parse_encrypted_token::parse_encrypted_token(const encrypted_token& value)
  : parse_encrypted_prefix(slice<0, 8>(value)),
    entropy_(slice<8, 16>(value)),
    sign_(slice<16, 17>(value)),
    data_(slice<17, 49>(value))
{
    valid(verify_magic() && verify_context() && verify_checksum(value));
}

hash_digest parse_encrypted_token::data() const
{
    return data_;
}

ek_entropy parse_encrypted_token::entropy() const
{
    return entropy_;
}

bool parse_encrypted_token::lot_sequence() const
{
    // There is no "flags" byte in token, we rely on prefix context.
    return context() == lot_context_;
}

one_byte parse_encrypted_token::sign() const
{
    return sign_;
}

bool parse_encrypted_token::verify_context() const
{
    return context() == default_context_ || context() == lot_context_;
}

bool parse_encrypted_token::verify_magic() const
{
    return slice<0, magic_size>(prefix()) == magic_;
}

} // namespace wallet
} // namespace libbitcoin
