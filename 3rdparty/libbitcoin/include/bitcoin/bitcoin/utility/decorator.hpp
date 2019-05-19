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
#ifndef LIBBITCOIN_DECORATOR_HPP
#define LIBBITCOIN_DECORATOR_HPP

#include <functional>

namespace libbitcoin {

/**
 * Defines a function decorator ala Python
 *
 *   void foo(int x, int y);
 *   function<void ()> wrapper(function<void (int)> f);
 *
 *   auto f = decorator(wrapper, bind(foo, 110, _1));
 *   f();
 */

template <typename Wrapper, typename Handler>
struct decorator_dispatch
{
    Wrapper wrapper;
    Handler handler;

    template <typename... Args>
    auto operator()(Args&&... args)
        -> decltype(wrapper(handler)(std::forward<Args>(args)...))
    {
        return wrapper(handler)(std::forward<Args>(args)...);
    }
};

template <typename Wrapper, typename Handler>
decorator_dispatch<Wrapper, typename std::decay<Handler>::type>
decorator(Wrapper&& wrapper, Handler&& handler)
{
    return { wrapper, handler };
}

} // namespace libbitcoin

#endif

