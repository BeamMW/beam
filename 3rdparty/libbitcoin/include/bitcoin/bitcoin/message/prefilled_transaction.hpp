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
#ifndef LIBBITCOIN_MESSAGE_PREFILLED_TRANSACTION_HPP
#define LIBBITCOIN_MESSAGE_PREFILLED_TRANSACTION_HPP

#include <istream>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/chain/transaction.hpp>
#include <bitcoin/bitcoin/math/hash.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>
#include <bitcoin/bitcoin/utility/reader.hpp>
#include <bitcoin/bitcoin/utility/writer.hpp>

namespace libbitcoin {
namespace message {

class BC_API prefilled_transaction
{
public:
    typedef std::vector<prefilled_transaction> list;
    typedef std::shared_ptr<const prefilled_transaction> const_ptr;

    static prefilled_transaction factory_from_data(uint32_t version,
        const data_chunk& data);
    static prefilled_transaction factory_from_data(uint32_t version,
        std::istream& stream);
    static prefilled_transaction factory_from_data(uint32_t version,
        reader& source);

    prefilled_transaction();
    prefilled_transaction(uint64_t index, const chain::transaction& tx);
    prefilled_transaction(uint64_t index, chain::transaction&& tx);
    prefilled_transaction(const prefilled_transaction& other);
    prefilled_transaction(prefilled_transaction&& other);

    uint64_t index() const;
    void set_index(uint64_t value);

    chain::transaction& transaction();
    const chain::transaction& transaction() const;
    void set_transaction(const chain::transaction& tx);
    void set_transaction(chain::transaction&& tx);

    bool from_data(uint32_t version, const data_chunk& data);
    bool from_data(uint32_t version, std::istream& stream);
    bool from_data(uint32_t version, reader& source);
    data_chunk to_data(uint32_t version) const;
    void to_data(uint32_t version, std::ostream& stream) const;
    void to_data(uint32_t version, writer& sink) const;
    bool is_valid() const;
    void reset();
    size_t serialized_size(uint32_t version) const;

    prefilled_transaction& operator=(prefilled_transaction&& other);
    prefilled_transaction& operator=(const prefilled_transaction& other);

    bool operator==(const prefilled_transaction& other) const;
    bool operator!=(const prefilled_transaction& other) const;

private:
    uint64_t index_;
    chain::transaction transaction_;
};

} // namespace message
} // namespace libbitcoin

#endif
