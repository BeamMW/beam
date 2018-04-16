#pragma once
#include "serialize_fwd.h"
#include "serialize_streams.h"
#include "yas/binary_iarchive.hpp"
#include "yas/binary_oarchive.hpp"
#include "yas/std_types.hpp"

namespace beam {

constexpr int SERIALIZE_OPTIONS = yas::binary | yas::no_header | yas::elittle | yas::compacted;

// TODO turn to shared buffers
using SerializeBuffer = std::pair<const char*, size_t>;

// TODO deprecating...
class Serializer {
public:
    Serializer() : _oa(_os) {}

    void reset() {
        _os.clear();
    }

    SerializeBuffer buffer() {
        return { _os.buf, _os.cur - _os.buf };
    }

    template <typename T> Serializer& operator&(const T& object) {
        _oa & object;
        return *this;
    }

    template <typename T> SerializeBuffer serialize(const T& object) {
        reset();
        _oa & object;
        return buffer();
    }

private:
    using Ostream = detail::SerializeOstream;

    Ostream _os;
    yas::binary_oarchive<Ostream, SERIALIZE_OPTIONS> _oa;
};

class Deserializer {
public:
    Deserializer() : _ia(_is) {}

    void reset(const void* buf, size_t size) {
        _is.reset(buf, size);
    }

    size_t bytes_left() {
        return _is.bytes_left();
    }

    template <typename T> bool deserialize(T& object) {
        try {
            _ia & object;
        } catch (...) {
            return false;
        }
        return true;
    }

    template <typename T> Deserializer& operator&(T& object) {
        _ia & object;
        return *this;
    }

private:
    using Istream = detail::SerializeIstream;

    Istream _is;
    yas::binary_iarchive<Istream, SERIALIZE_OPTIONS> _ia;
};

} //namespace

