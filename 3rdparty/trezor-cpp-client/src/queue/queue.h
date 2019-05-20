#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>

template <typename T>
class Queue
{
public:

    Queue() : m_pop_lock(false)
    {
    }

    Queue (const Queue&) = delete;            // disable copying
    Queue& operator= (const Queue&) = delete; // disable assignment

    bool pop (T& item, size_t timeout = 200)
    {
        if (!m_pop_lock.load())
        {
            std::unique_lock<std::mutex> mlock(m_mutex);
            if (m_cond.wait_for(mlock, std::chrono::milliseconds(timeout), [this] { return !m_deque.empty(); }))
            {
                item = std::move(m_deque.front());
                m_deque.pop_front();
                return true;
            }
        }
        return false; 
    }

    void push (const T& item)
    {
        std::unique_lock<std::mutex> mlock(m_mutex);
        m_deque.push_back(item);
        mlock.unlock();
        m_cond.notify_one();
    }

    void clear ()
    {
        m_deque.clear();
    }

    size_t size()
    {
        return m_deque.size();
    }

    void unlockPop()
    {
        m_pop_lock = false;
    }

    void lockPop()
    {
        m_pop_lock = true;
    }

protected:
    std::deque<T> m_deque;
    std::mutex m_mutex;

private:
    std::atomic_bool m_pop_lock;
    std::condition_variable m_cond;
};
