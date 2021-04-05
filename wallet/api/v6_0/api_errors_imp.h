// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include "../api_errors.h"
#include <stdexcept>

namespace beam::wallet
{
    const char* getApiErrorMessage(ApiError code);

    class jsonrpc_exception: public std::runtime_error
    {
    public:
        explicit jsonrpc_exception(const ApiError code, const std::string &msg = "")
            : runtime_error(msg)
            , _code(code)
        {
        }

        [[nodiscard]] ApiError code() const
        {
            return _code;
        }

        [[nodiscard]] std::string whatstr() const
        {
            return std::string(what());
        }

        [[nodiscard]] bool has_what() const
        {
            return what() && std::char_traits<char>::length(what());
        }

    private:
        ApiError _code;
    };
}
