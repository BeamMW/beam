#pragma once
#include "serialize_fwd.h"
#include "serialize_streams.h"
#include "yas/binary_iarchive.hpp"
#include "yas/binary_oarchive.hpp"
#include "yas/std_types.hpp"

namespace beam {

/// Yas lib options
constexpr int SERIALIZE_OPTIONS = yas::binary | yas::no_header | yas::elittle | yas::compacted;

/// Raw data reference
using SerializeBuffer = std::pair<const char*, size_t>;

/// Serializer to contiguous buffer
template <size_t BUFFER_SIZE> class StaticBufferSerializer {
public:
    StaticBufferSerializer() : _oa(_os) {}

    void reset() {
        _os.clear();
    }

    SerializeBuffer buffer() {
        return { (const char*) &_os.m_vec.at(0), _os.m_vec.size() };
    }

    template <typename T> StaticBufferSerializer& operator&(const T& object) {
        _oa & object;
        return *this;
    }

    template <typename T> SerializeBuffer serialize(const T& object) {
        reset();
        _oa & object;
        return buffer();
    }

private:
    using Ostream = detail::SerializeOstreamStatic<BUFFER_SIZE>;

    Ostream _os;
    yas::binary_oarchive<Ostream, SERIALIZE_OPTIONS> _oa;
};

/// Default serializer has 100K buffer inside
using DefStaticSerializer = StaticBufferSerializer<100*1024>;

/// Serializer to growing buffer
class Serializer {
public:
    Serializer() : _oa(_os) {}

    void reset() {
        _os.clear();
    }

    SerializeBuffer buffer() {
        return { (const char*) &_os.m_vec.at(0), _os.m_vec.size() };
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

	void swap_buf(std::vector<uint8_t>& v) { _os.m_vec.swap(v); }

private:
    using Ostream = detail::SerializeOstream;

    Ostream _os;
    yas::binary_oarchive<Ostream, SERIALIZE_OPTIONS> _oa;
};

/// Deserializer from static buffer
class Deserializer {
public:
    /// Ctor
    Deserializer() : _ia(_is) {}

    /// Resets to new input buffer
    void reset(const void* buf, size_t size) {
        _is.reset(buf, size);
    }

    /// Returns bytes unconsumed from the buffer
    size_t bytes_left() {
        return _is.bytes_left();
    }

    /// Deserializes arbitrary object and suppresses yas exception
    template <typename T> bool deserialize(T& object) {
        try {
            _ia & object;
        } catch (...) {
            return false;
        }
        return true;
    }

    /// Deserializes whatever from the buffer
    template <typename T> Deserializer& operator&(T& object) {
        _ia & object;
        return *this;
    }

private:
    /// Contiguous buffer istream
    using Istream = detail::SerializeIstream;

    Istream _is;
    yas::binary_iarchive<Istream, SERIALIZE_OPTIONS> _ia;
};

} //namespace

