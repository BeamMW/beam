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
#include <bitcoin/bitcoin/utility/interprocess_lock.hpp>

#include <memory>
#include <string>
#include <boost/filesystem.hpp>
#include <bitcoin/bitcoin/unicode/file_lock.hpp>
#include <bitcoin/bitcoin/unicode/ofstream.hpp>

namespace libbitcoin {

// static
bool interprocess_lock::create(const std::string& file)
{
    bc::ofstream stream(file);
    return stream.good();
}

// static
bool interprocess_lock::destroy(const std::string& file)
{
    return boost::filesystem::remove(file);
    ////std::remove(file.c_str());
}

interprocess_lock::interprocess_lock(const path& file)
  : file_(file.string())
{
}

interprocess_lock::~interprocess_lock()
{
    unlock();
}

// Lock is not idempotent, returns false if already locked.
// This succeeds if no other process has exclusive or sharable ownership.
bool interprocess_lock::lock()
{
    if (!create(file_))
        return false;

    lock_ = std::make_shared<lock_file>(file_);
    return lock_->try_lock();
}

// Unlock is idempotent, returns true if unlocked on return.
// This may leave the lock file behind, which is not a problem.
bool interprocess_lock::unlock()
{
    if (!lock_)
        return true;

    lock_.reset();
    return destroy(file_);
}

} // namespace libbitcoin
