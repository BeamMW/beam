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
#include <string>
#include <boost/filesystem.hpp>

namespace beam::fsutils
{
    using namespace boost::filesystem;

    bool exists(const path& path);
    bool exists(const std::string& path);

    void remove(const path& path); // throws
    void remove(const std::string& path); // throws

    void rename(const path& oldp, const path& newp); // throws
    void rename(const std::string& oldp, const std::string& newp); // throws

    std::vector<uint8_t> fread(const path& path); // throws
    std::vector<uint8_t> fread(const std::string& path); // throws
}
