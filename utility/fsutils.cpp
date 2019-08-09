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

#include <boost/filesystem.hpp>
#include "common.h"
#include "logger.h"
#include "fsutils.h"

namespace beam::fsutils
{
    bool remove(const boost::filesystem::path& path)
    {
        boost::system::error_code error;
        boost::filesystem::remove(path, error);
        if (error) LOG_ERROR() << "fsutils::remove " << path << " error: " << error.message();
        return !static_cast<bool>(error);
    }

    bool remove(const std::string& spath)
    {
#ifdef WIN32
        boost::filesystem::path path(Utf8toUtf16(spath));
        return fsutils::remove(path);
#else
        boost::filesystem::path path(spath);
        return fsutils::remove(path);
#endif
    }
}
