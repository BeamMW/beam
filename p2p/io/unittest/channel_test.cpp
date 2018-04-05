#include "../asyncevent.h"
#include <future>
#include <unistd.h>
#include <iostream>
#include <assert.h>

#include <deque>
#include <mutex>

using namespace io;
using namespace std;

template <class T> class MessageQueue {
public:
    bool send(const T& message) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_rxClosed) return false;
        _queue.push_back(message);
        return true;
    }
    
    bool send(T&& message) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_rxClosed) return false;
        _queue.push_back(std::move(message));
        return true;
    }
    
    size_t current_size() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }
    
    bool receive(T& message) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) return false;
        message = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }
    
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

template <class T> class TX {
public:
    bool send(const T& message) {
        return _queue->send(message) && _asyncEvent->trigger();
    }
    
    bool send(T&& message) {
        return _queue->send(std::move(message)) && _asyncEvent->trigger();
    }
    
    size_t queue_size() {
        return _queue.current_size();
    }
    
private:
    template <class> friend class RX; // RX creates TX-es
    
    TX(const std::shared_ptr<MessageQueue<T>>& queue, const AsyncEvent::Ptr& asyncEvent) :
        _queue(queue), _asyncEvent(asyncEvent)
    {}
    
    std::shared_ptr<MessageQueue<T>> _queue;
    AsyncEvent::Ptr _asyncEvent;
};

template <class T> class RX {
public:
    using Callback = std::function<void(T&& message)>;
    
    explicit RX(const Reactor::Ptr& reactor, Callback&& callback) :
        _queue(std::make_shared<MessageQueue<T>>()),
        _asyncEvent(AsyncEvent::create(reactor, [this]() { on_receive(); } )),
        _callback(std::move(callback))
    {
        assert(_asyncEvent); //TODO this is runtime error, throw..
        assert(_callback);
    }
    
    ~RX() {
        _queue->close_rx();
    }
    
    TX<T> get_tx() {
        return TX<T>(_queue, _asyncEvent);
    }
    
    size_t queue_size() {
        return _queue.current_size();
    }
    
private:
    void on_receive() {
        while (_queue->receive(_msg)) {
            _callback(std::move(_msg)); 
        }
    }
    
    std::shared_ptr<MessageQueue<T>> _queue;
    AsyncEvent::Ptr _asyncEvent;
    Callback _callback;
    T _msg;
};

static const string testStr("some moveble data");
static const Config config;

struct Message {
    int n=0;
    unique_ptr<string> d;
};

struct SomeAsyncObject {
    Reactor::Ptr reactor;
    std::future<void> f;
    
    SomeAsyncObject() :
        reactor(Reactor::create(config)) 
    {}
    
    void run() {
        f = std::async(
            std::launch::async,
            [this]() {
                reactor->run();
            }
        );
    }
    
    void wait() { f.get(); }
};

struct RXThread : SomeAsyncObject {
    RX<Message> rx;
    std::vector<int> received;
    
    RXThread() :
        rx(
            reactor, 
            [this](Message&& msg) {
                if (msg.n == 0) {
                    reactor->stop();
                    return;
                }
                if (*msg.d != testStr) {
                    received.push_back(0);
                } else {
                    received.push_back(msg.n);
                }
            }
        )
    {}
};

void simplex_channel_test() {
    RXThread remote;
    TX<Message> tx = remote.rx.get_tx();
    std::vector<int> sent;
    
    remote.run();
    
    for (int i=1; i<=100500; ++i) {
        tx.send(Message { i, make_unique<string>(testStr) } );
        sent.push_back(i);
    }
    
    tx.send(Message { 0, make_unique<string>(testStr) } );
    
    remote.wait();
    
    assert(remote.received == sent);
}

int main() {
    simplex_channel_test();
}

