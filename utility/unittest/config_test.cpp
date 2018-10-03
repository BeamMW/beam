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

#include "utility/config.h"
#include <iostream>
#include <fstream>
#include <assert.h>
#ifndef WIN32
#include <unistd.h>
#endif

using namespace beam;
using namespace std;

static int error_count = 0;

#define CHECK(s) \
do {\
    assert(s);\
    if (!(s)) {\
        ++error_count;\
    }\
} while(false)\



void load_config() {
    static const std::string fileName("/tmp/xoxoxo");

#ifdef WIN32
    _unlink(fileName.c_str());
#else
    unlink(fileName.c_str());
#endif // WIN32

    std::ofstream file(fileName);
    file << R"({
# comments may go after '#' character
        "sos": 222,
        "nullvalue": null, # null values ignored
        "schnaps" : {
            "keks" : "sex",
            "ogogo" : "sdjd#kj\"h#ss", # '#'s inside quotes are passed
            "zzz" : -404040, # a comment
        # here is comment
            "ooo" : true,
            "dadanetda": [true,true,false, true]
        },
        "ports": [2,3,4,5,6,7,8,9]}
    )";
    file.close();

    Config config;
    config.load(fileName);
    reset_global_config(std::move(config));
}

void test_config() {
    load_config();
    try {
        load_config();
        assert(false && "must throw on loading config twice");
    } catch (...) {
    }

    CHECK(!config().has_key("slon"));
    CHECK(!config().has_key("nullvalue"));
    CHECK(config().has_key("schnaps.ooo"));
    auto s = config().get_string("schnaps.keks");
    CHECK(s == "sex");
    auto i = config().get_int("schnaps.zzz", 0);
    CHECK(i == -404040);
    auto v = config().get_int_list("ports");
    CHECK(v.size() == 8 && v[4] == 6);
    i = config().get_int("sos");
    CHECK(i == 222);
    CHECK(config().get_bool_list("schnaps.dadanetda") == std::vector<bool>({true,true,false,true}));
}

int main() {
    try {
        test_config();
    }
    catch (const exception& e) {
        cout << "Exception: " << e.what() << '\n';
    }
    catch(...) {
        cout << "Non-std exception\n";
    }
    return error_count;
}

