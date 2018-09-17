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
#include "io/asyncevent.h"
#include <mutex>
#include <deque>
#include <assert.h>

namespace beam {

/// Inter-thread message queue, backend for RX and TX sides (see below)
/// Current impl (may be changed if performance bottleneck detected):
/// 1) unlimited size - should be controlled by channel sides explicitly;
/// 2) std::deque and std::mutex inside
/// Message type (class T) requirement: default constructible + callable *or* movable (see send() functions)
template <class T> class MessageQueue {
public:
    /// Called from sender thread via TX object
    bool send(const T& message) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_rxClosed) return false;
        _queue.push_back(message);
        return true;
    }

    /// Called from sender thread via TX object
    bool send(T&& message) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_rxClosed) return false;
        _queue.push_back(std::move(message));
        return true;
    }

    /// May be called by both TX and RX
    size_t current_size() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

    /// Called from receiver thread via RX object
    bool receive(T& message) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) return false;
        message = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    /// Called by RX to indicate that the channel is being closed
    void close_rx() {
        std::lock_guard<std::mutex> lock(_mutex);
        _rxClosed = true;
    }

private:
    std::mutex _mutex;
    // TODO consider spinlock

    std::deque<T> _queue;

    bool _rxClosed=false;
    // TODO consider atomic flag
};

/// Transmitter side of inter-thread channel
template <class T> class TX {
public:

    bool send(const T& message) {
        return _queue->send(message) && _asyncEvent();
    }

    bool send(T&& message) {
        return _queue->send(std::move(message)) && _asyncEvent();
    }

    size_t queue_size() {
        return _queue.current_size();
    }

private:
    template <class> friend class RX; // friend because RX creates TX-es

    /// Ctor called by RX, see friendship
    TX(const std::shared_ptr<MessageQueue<T>>& queue, const io::AsyncEvent::Ptr& asyncEvent) :
        _queue(queue), _asyncEvent(asyncEvent)
    {}

    /// Queue
    std::shared_ptr<MessageQueue<T>> _queue;

    /// io::Reactor event that can be called from another thread
    io::AsyncEvent::Trigger _asyncEvent;
};

/// Receiver side of inter=thread channel
template <class T> class RX {
public:
    /// Message callback, called from reactor thread
    using Callback = std::function<void(T&& message)>;

    /// Ctor called by receiver side
    explicit RX(io::Reactor& reactor, Callback&& callback) :
        _queue(std::make_shared<MessageQueue<T>>()),
        _asyncEvent(io::AsyncEvent::create(reactor, [this]() { on_receive(); } )),
        _callback(std::move(callback))
    {
        assert(_asyncEvent); //TODO this is runtime error, throw..
        assert(_callback);
    }

    /// Dtor closes channel
    ~RX() {
        close();
    }

    /// RX creates TXes
    TX<T> get_tx() {
        return TX<T>(_queue, _asyncEvent);
    }

    size_t queue_size() {
        return _queue.current_size();
    }

    void close() {
        _queue->close_rx();
    }

private:
    void on_receive() {
        while (_queue->receive(_msg)) {
            _callback(std::move(_msg));
        }
    }

    std::shared_ptr<MessageQueue<T>> _queue;
    io::AsyncEvent::Ptr _asyncEvent;
    Callback _callback;

    /// Message must be default-constructible
    T _msg;
};

} //namespace

