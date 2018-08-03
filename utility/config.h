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
#include <stdint.h>
#include <unordered_map>
#include <vector>
#if __has_include(<any>)
    #include <any>
    using std::any;
    using std::any_cast;
#else
    // TODO temporary workaround
    #include <boost/any.hpp>
    using boost::any;
    using boost::any_cast;
#endif

namespace beam {

class Config;

/// Returns global config
const Config& config();

/// Assigns global config once configuration is done.
/// throws if already assigned!
void reset_global_config(Config&& c);

class Config {
public:
    // types used when JSON config loaded
    using Int = int64_t;
    using StringList = std::vector<std::string>;
    using IntList = std::vector<int64_t>;
    using FloatList = std::vector<double>;
    using BoolList = std::vector<bool>;

    Config() = default;
    Config(const Config& r) = default;
    Config& operator=(const Config& r) = default;
    Config(Config&&) = default;
    Config& operator=(Config&&) = default;

    bool empty() const {
        return _values.empty();
    }

    /// Loads from json file and throws on error
    void load(const std::string& fileName);

    // todo: load from environment

    template<typename T> void set(const std::string& key, T&& value) {
        _values[key] = any(std::move(value));
    }

    template<typename T> void set(const std::string& key, const T& value) {
        _values[key] = any(value);
    }

    bool has_key(const std::string& key) const {
        return _values.count(key) == 1;
    }

    template <typename T> const T& get(const std::string& key, const T& defValue=T()) const {
        auto it = _values.find(key);
        if (it == _values.end()) return defValue;
        const T* value = any_cast<T>(&it->second);
        return value ? *value : defValue;
    }

    const std::string& get_string(const std::string& key, const std::string& defValue=std::string()) const {
        return get<std::string>(key);
    }

    int get_int(
        const std::string& key,
        int defValue=0,
        int minValue=std::numeric_limits<int>::min(),
        int maxValue=std::numeric_limits<int>::max()
    ) const {
        int val = int(get<Int>(key, defValue));
        if (val <= minValue) return minValue;
        if (val >= maxValue) return maxValue;
        return val;
    }
    
    Int get_i64(const std::string& key) const {
        return get<Int>(key, -1);
    }

    const IntList& get_int_list(const std::string& key) const {
        return get<IntList>(key);
    }

    const BoolList& get_bool_list(const std::string& key) const {
        return get<BoolList>(key);
    }

    // ~etc

private:
    std::unordered_map<std::string, any> _values;
};

} //namespace

