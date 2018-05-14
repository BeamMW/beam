#pragma once
#include <shared_mutex>

namespace beam {

template <class Data> class SharedData {
    mutable std::shared_mutex _rwLock;
    Data _data;

public:
    
    class Reader {
    public:
        Reader(const Reader&)=delete;
        Reader& operator=(const Reader&)=delete;

        Reader(Reader&& r) : _owner(r._owner) {
            r._owner = 0;
        }

        ~Reader() {
            if (_owner) _owner->_rwLock.unlock_shared();
        }

        operator bool() const {
            return _owner != 0;
        }

        const Data* operator->() const {
            return &(_owner->_data);
        }

        const Data& operator*() const {
            return _owner->_data;
        }

    private:
        using Owner = SharedData<Data>;
        friend Owner;

        Reader(const Owner* owner) : _owner(owner)
        {}

        const Owner* _owner;
    };

    class Writer {
    public:
        Writer(const Writer&)=delete;
        Writer& operator=(const Writer&)=delete;

        Writer(Writer&& r) : _owner(r._owner) {
            r._owner = 0;
        }

        ~Writer() {
            if (_owner) _owner->_rwLock.unlock();
        }

        operator bool() {
            return _owner != 0;
        }

        Data* operator->() {
            return &(_owner->_data);
        }

        Data& operator*() {
            return _owner->_data;
        }

    private:
        using Owner = SharedData<Data>;
        friend Owner;

        Writer(Owner* owner) : _owner(owner)
        {}

        Owner* _owner;
    };

    SharedData() {}
    
    SharedData(SharedData&& d) :
        _rwLock(std::move(d._rwLock)),
        _data(std::move(d._data))
    {}
    
    SharedData& operator=(SharedData&& d) {
        _rwLock = std::move(d._rwLock);
        _data = std::move(d._data);
        return *this;
    }

    SharedData(const Data& d) : _data(d) {}
    
    SharedData(Data&& d) : _data(std::move(d)) {}
    
    template<typename ...Args> SharedData(Args&& ... args) : _data(std::forward<Args...>(args...)) {}

    Reader read() const {
        _rwLock.lock_shared();
        return Reader(this);
    }

    const Reader try_read() const {
        bool locked = _rwLock.try_lock_shared();
        return Reader(locked ? this : 0);
    }

    Writer write() {
        _rwLock.lock();
        return Writer(this);
    }

    Writer try_write() const {
        bool locked = _rwLock.try_lock();
        return Writer(locked ? this : 0);
    }

private:
    friend Reader;
    friend Writer;
};

} //namespace

