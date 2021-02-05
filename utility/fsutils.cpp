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
    namespace {
        path topath(const std::string& spath)
        {
            #ifdef WIN32
            return path (Utf8toUtf16(spath.c_str()));
            #else
            return path (spath);
            #endif
        }
    }

    bool exists(const boost::filesystem::path& path)
    {
        return boost::filesystem::exists(path);
    }

    bool exists(const std::string& path)
    {
        return fsutils::exists(topath(path));
    }

    void remove(const path& path)
    {
        boost::system::error_code error;
        boost::filesystem::remove(path, error);

        if (error)
        {
            auto errmsg = std::string("fsutils::remove for ") + path.string() + ", " + error.message();
            throw std::runtime_error(errmsg);
        }
    }

    void remove(const std::string& path)
    {
        return fsutils::remove(topath(path));
    }

    void rename(const path& oldPath, const path& newPath)
    {
        boost::system::error_code error;
        boost::filesystem::rename(oldPath, newPath, error);

        if (error)
        {
            auto errmsg = std::string("fsutils::rename ") + oldPath.string() + " -> " + newPath.string() + ", " + error.message();
            throw std::runtime_error(errmsg);
        }
    }

    void rename(const std::string& oldPath, const std::string& newPath)
    {
        return fsutils::rename(topath(oldPath), topath(newPath));
    }

    std::vector<uint8_t> fread(const path& path)
    {
        std::ifstream file;

        auto checkerr = [&path, &file] () {
            if (file.fail())
            {
                std::ostringstream ss;
                ss << "fsutils::fread failed for file " << path.string() + ", code " << errno << ", msg " << strerror(errno);
                throw std::runtime_error(ss.str());
            }
        };

        file.open(path, std::ios::binary);
        checkerr();

        file.unsetf(std::ios::skipws);
        file.seekg(0, std::ios::end);
        const auto fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        checkerr();

        if (!fileSize)
        {
            return std::vector<uint8_t>();
        }

        std::vector<uint8_t> vec;
        vec.resize(fileSize);

        file.read(reinterpret_cast<char*>(&vec[0]), fileSize);
        checkerr();

        return vec;
    }

    std::vector<uint8_t> fread(const std::string& path)
    {
        return fsutils::fread(topath(path));
    }
}
