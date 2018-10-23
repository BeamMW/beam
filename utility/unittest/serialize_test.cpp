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

#include "utility/serialize.h"
#include "utility/helpers.h"
#include <iostream>

using namespace beam;

namespace yas {
namespace detail {

/***************************************************************************/

template<std::size_t F, typename T>
struct serializer<
	type_prop::not_a_fundamental,
	ser_method::use_internal_serializer,
	F,
	std::unique_ptr<T>
> {
	template<typename Archive>
	static Archive& save(Archive& ar, const std::unique_ptr<T>& ptr) {
        T* t = ptr.get();
        if (t) {
            ar & true;
            ar & *t;
        } else {
            ar & false;
        }
		return ar;
	}

	template<typename Archive>
	static Archive& load(Archive& ar, std::unique_ptr<T>& ptr) {
        bool b=false;
        ar & b;
        if (b) {
            ptr.reset(new T());
            ar & *ptr;
        } else {
            ptr.reset();
        }
        return ar;
	}
};

template<std::size_t F, typename T>
struct serializer<
	type_prop::not_a_fundamental,
	ser_method::use_internal_serializer,
	F,
	std::shared_ptr<T>
> {
	template<typename Archive>
	static Archive& save(Archive& ar, const std::shared_ptr<T>& ptr) {
        T* t = ptr.get();
        if (t) {
            ar & true;
            ar & *t;
        } else {
            ar & false;
        }
		return ar;
	}

	template<typename Archive>
	static Archive& load(Archive& ar, std::shared_ptr<T>& ptr) {
        bool b=false;
        ar & b;
        if (b) {
            ptr.reset(new T());
            ar & *ptr;
        } else {
            ptr.reset();
        }
        return ar;
	}
};


/***************************************************************************/

} // namespace detail
} // namespace yas

struct X {
    int i=333;
    std::vector<std::string> x;
    std::unordered_set<uint64_t> y;
    bool b=false;

    SERIALIZE(i,x,y,b);
};

bool operator==(const X& x, const X &y) {
    return x.i == y.i && x.x == y.x && x.y ==y.y && x.b == y.b;
}

int main() {
    using namespace std;

    uint16_t v0 = 128, v1 = 0;
    string s0("ABCDE"), s1;
    unique_ptr<X> p0(new X()), p1;
    shared_ptr<X> sp0(new X()), sp1;

    X x0 { 31, { "xxx", "yyy", "abcdefghijklmnopqrstuvwxyz" }, { uint64_t(-1), 20, 30, 40}, true };
    X x1;

    Serializer ser;

    ser & x0 & v0 & s0 & p0 & sp0;

    auto [buf, size] = ser.buffer();

    Deserializer des;
    des.reset(buf, size);

    des & x1 & v1 & s1 & p1 & sp1;

    assert ( v0 == v1 && s0 == s1 && x0 == x1 && p1->i == 333 && sp1->i == 333);

    char xxx[80];
    cout << '\n' << format_timestamp(xxx, 80, "%Y-%m-%d.%T", local_timestamp_msec()) << " bytes for " << xxx << '\n';
}
