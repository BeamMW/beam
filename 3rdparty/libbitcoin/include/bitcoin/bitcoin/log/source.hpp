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
#ifndef LIBBITCOIN_LOG_SOURCE_HPP
#define LIBBITCOIN_LOG_SOURCE_HPP

#include <string>
#include <boost/log/attributes/clock.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/log/attributes.hpp>
#include <bitcoin/bitcoin/log/severity.hpp>

namespace libbitcoin {
namespace log {

typedef boost::log::sources::severity_channel_logger_mt<severity, std::string>
    severity_source;

BOOST_LOG_INLINE_GLOBAL_LOGGER_INIT(source, severity_source)
{
    static const auto name = attributes::timestamp.get_name();
    severity_source logger;
    logger.add_attribute(name, boost::log::attributes::utc_clock());
    return logger;
}

#define BC_LOG_SEVERITY(id, level) \
    BOOST_LOG_CHANNEL_SEV(bc::log::source::get(), id, bc::log::severity::level)

#define LB_LOG_VERBOSE(module) BC_LOG_SEVERITY(module, verbose)
#define LB_LOG_DEBUG(module) BC_LOG_SEVERITY(module, debug)
#define LB_LOG_INFO(module) BC_LOG_SEVERITY(module, info)
#define LB_LOG_WARNING(module) BC_LOG_SEVERITY(module, warning)
#define LB_LOG_ERROR(module) BC_LOG_SEVERITY(module, error)
#define LB_LOG_FATAL(module) BC_LOG_SEVERITY(module, fatal)

} // namespace log
} // namespace libbitcoin

#endif
