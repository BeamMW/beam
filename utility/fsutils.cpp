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

    bool exists(const path& fpath)
    {
        return std::filesystem::exists(fpath);
    }

    bool exists(const std::string& spath)
    {
        return fsutils::exists(topath(spath));
    }

    void remove(const path& fpath)
    {
        std::error_code error;
        std::filesystem::remove(fpath, error);

        if (error)
        {
            auto errmsg = std::string("fsutils::remove for ") + fpath.string() + ", " + error.message();
            throw std::runtime_error(errmsg);
        }
    }

    void remove(const std::string& spath)
    {
        return fsutils::remove(topath(spath));
    }

    void rename(const path& oldPath, const path& newPath)
    {
        std::error_code error;
        std::filesystem::rename(oldPath, newPath, error);

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

    std::vector<uint8_t> fread(const path& fpath)
    {
        std::ifstream file;

        auto checkerr = [&fpath, &file] () {
            if (file.fail())
            {
                std::ostringstream ss;
                ss << "fsutils::fread failed for file " << fpath.string() + ", code " << errno;
                throw std::runtime_error(ss.str());
            }
        };

        file.open(fpath, std::ios::binary);
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

    std::vector<uint8_t> fread(const std::string& spath)
    {
        return fsutils::fread(topath(spath));
    }
}
