#include "utility/config.h"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <assert.h>

using namespace beam;
using namespace std;

void load_config() {
    static const std::string fileName("/tmp/xoxoxo");
    unlink(fileName.c_str());

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

    assert(!config().has_key("slon"));
    assert(!config().has_key("nullvalue"));
    assert(config().has_key("schnaps.ooo"));
    auto s = config().get_string("schnaps.keks");
    assert(s == "sex");
    auto i = config().get_int("schnaps.zzz");
    assert(i == -404040);
    auto v = config().get_int_list("ports");
    assert(v.size() == 8 && v[4] == 6);
    i = config().get_int("sos");
    assert(i == 222);
    assert(config().get_bool_list("schnaps.dadanetda") == std::vector<bool>({true,true,false,true}));
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
}

