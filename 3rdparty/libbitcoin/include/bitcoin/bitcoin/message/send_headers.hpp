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
#ifndef LIBBITCOIN_MESSAGE_SEND_HEADERS_HPP
#define LIBBITCOIN_MESSAGE_SEND_HEADERS_HPP

#include <istream>
#include <memory>
#include <string>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>
#include <bitcoin/bitcoin/utility/reader.hpp>
#include <bitcoin/bitcoin/utility/writer.hpp>

namespace libbitcoin {
namespace message {

class BC_API send_headers
{
public:
    typedef std::shared_ptr<send_headers> ptr;
    typedef std::shared_ptr<const send_headers> const_ptr;

    static send_headers factory_from_data(uint32_t version,
        const data_chunk& data);
    static send_headers factory_from_data(uint32_t version,
        std::istream& stream);
    static send_headers factory_from_data(uint32_t version,
        reader& source);
    static size_t satoshi_fixed_size(uint32_t version);

    send_headers();
    send_headers(const send_headers& other);
    send_headers(send_headers&& other);

    bool from_data(uint32_t version, const data_chunk& data);
    bool from_data(uint32_t version, std::istream& stream);
    bool from_data(uint32_t version, reader& source);
    data_chunk to_data(uint32_t version) const;
    void to_data(uint32_t version, std::ostream& stream) const;
    void to_data(uint32_t version, writer& sink) const;
    bool is_valid() const;
    void reset();
    size_t serialized_size(uint32_t version) const;

    static const std::string command;
    static const uint32_t version_minimum;
    static const uint32_t version_maximum;

protected:
    send_headers(bool insufficient_version);

private:
    bool insufficient_version_;
};

} // namespace message
} // namespace libbitcoin

#endif
