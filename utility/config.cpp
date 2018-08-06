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

#include "config.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <exception>

//#include <iostream>

namespace beam {

namespace {

using json = nlohmann::json;

void filter_comments(char* line) {
    char* p = line;
    char prev = *p;
    if (!prev) return;
    if (prev == '#') {
        *p = 0;
        return;
    }
    int nQuotes = 0;
    for (++p; *p; ++p) {
        char c = *p;
        if (c == '#' && (nQuotes % 2) == 0) {
            *p = 0;
            return;
        }
        if (c == '"' && prev != '\\') ++nQuotes;
        prev = c;
    }
}

std::string load_and_filter(const std::string& fileName) {
    constexpr size_t LINESIZE = 2048;
    char buf[LINESIZE];

    std::ifstream file(fileName);
    if (!file) throw std::runtime_error(std::string("cannot open config file ") + fileName);

    std::string filtered;

    while (file.getline(buf, LINESIZE)) {
        filter_comments(buf);
        filtered.append(buf);
    }

   // std::cout << filtered << "\n\n";
    return filtered;
}

using Values = std::unordered_map<std::string, any>;

template <typename T, typename ...Args> any array_values(const json& o) {
    std::vector<T, Args...> vec;
    for (const auto& x : o) {
        vec.push_back(x.get<T>());
    }
    return any(std::move(vec));
}

void add_array(Values& v, const json& o, const std::string& name) {
    if (o.empty()) return;
    switch (o[0].type()) {
        case json::value_t::string:
            v[name] = array_values<std::string>(o);
            break;
        case json::value_t::number_integer:
        case json::value_t::number_unsigned:
            v[name] = array_values<int64_t>(o);
            break;
        case json::value_t::boolean:
            v[name] = array_values<bool>(o);
            break;
        case json::value_t::number_float:
            v[name] = array_values<double>(o);
            break;
        default:
            break;
    }
}

void add_object(Values& v, const json& o, const std::string& name) {
    switch (o.type()) {
        case json::value_t::number_integer:
        case json::value_t::number_unsigned:
            v[name] = any(o.get<int64_t>());
            break;
        case json::value_t::boolean:
            v[name] = any(o.get<bool>());
            break;
        case json::value_t::string:
            v[name] = any(o.get<std::string>());
            break;
        case json::value_t::number_float:
            v[name] = any(o.get<double>());
            break;
        case json::value_t::object:
            for (json::const_iterator it = o.begin(); it != o.end(); ++it) {
                add_object(v, it.value(), name + "." + it.key());
            }
            break;
        case json::value_t::array:
            add_array(v, o, name);
            break;
        default:
            break;
    }
}

} //namespace

void Config::load(const std::string& fileName) {
    std::string filtered = load_and_filter(fileName);
    if (filtered.empty()) throw std::runtime_error(std::string("empty config ") + fileName);

    json j = json::parse(filtered);
    if (!j.is_object()) throw std::runtime_error(std::string("bad config format ") + fileName);

    for (json::iterator it = j.begin(); it != j.end(); ++it) {
//        std::cout << it.key() << "::::" << it.value() << '\n';
        add_object(_values, it.value(), it.key());
    }
/*
    for (auto p : _values) {
        std::cout << p.first << '\n';
    }
*/
}

static Config g_config;

const Config& config() {
    return g_config;
}

void reset_global_config(Config&& c) {
    if (!g_config.empty()) throw std::runtime_error("reset non-empty config");
    g_config = std::move(c);
}

} //namespace

