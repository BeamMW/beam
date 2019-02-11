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
#include "serialize_fwd.h"
#include "serialize_streams.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4127 4458)
#endif

#include "yas/binary_iarchive.hpp"
#include "yas/binary_oarchive.hpp"
#include "yas/std_types.hpp"

#define YAS_SERIALIZE_BOOST_TYPES
#include "yas/types/boost/optional.hpp"
#undef YAS_SERIALIZE_BOOST_TYPES

#ifdef _MSC_VER
#pragma warning(pop)
#endif

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

/// Size counter, doesn't store anything
struct SerializerSizeCounter
{
	struct Counter
	{
		size_t m_Value; // should not overflow, since it's used only for objects of limited size (tx elements, etc.)

		size_t write(const void * /*ptr*/, const size_t size)
		{
			m_Value += size;
			assert(m_Value >= size); // no overflow
			return size;
		}

	} m_Counter;

	yas::binary_oarchive<Counter, SERIALIZE_OPTIONS> _oa;


	SerializerSizeCounter() : _oa(m_Counter)
	{
		m_Counter.m_Value = 0;
	}

	template <typename T> SerializerSizeCounter& operator & (const T& object)
	{
		_oa & object;
		return *this;
	}
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

	void reset(const std::vector<uint8_t>& bb) {
		reset(bb.empty() ? nullptr : &bb.front(), bb.size());
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

