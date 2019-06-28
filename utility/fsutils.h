#pragma once

#include <string>
#include <boost/filesystem.hpp>

namespace beam::fsutils {
    // remove file, log error if any
    bool remove(const boost::filesystem::path& path);
    bool remove(const std::string& path);
}
