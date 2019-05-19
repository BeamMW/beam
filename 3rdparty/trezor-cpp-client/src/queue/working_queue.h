#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <tuple>
#include "queue.h"
#include "models/call.hpp"

template <typename O, typename I>
using InQueueFunction = std::function<O(const I &)>;
template <typename O, typename I>
using PopCallback = std::function<void(const I &, const O &)>;
template <typename O, typename I>
using InQueueItem = std::tuple<I, InQueueFunction<O, I>, PopCallback<O, I>>;

template <typename O, typename I>
class WorkingQueue : public Queue<InQueueItem<O, I>>
{
  public:
    WorkingQueue()
        : m_thread(&WorkingQueue::_threadMain, this),
          m_globalPopCallback(nullptr),
          m_PauseMillis(0),
          m_pushLock(false)
    {
    }

    ~WorkingQueue()
    {
        m_alive.clear();
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void lockPush()
    {
        m_pushLock = true;
    }

    void unlockPush()
    {
        m_pushLock = false;
    }

    void push(const I &args, InQueueFunction<O, I> function, PopCallback<O, I> callback = nullptr)
    {
        if (!m_pushLock)
        {
            if (!replase(args, function, callback))
            {
                Queue<InQueueItem<O, I>>::push(std::make_tuple(args, function, callback));
            }
        }
    }

    void remove(const I &args)
    {
        std::unique_lock<std::mutex> mlock(Queue<InQueueItem<O, I>>::m_mutex);

        auto &deque = Queue<InQueueItem<O, I>>::m_deque;
        deque.erase(
            std::remove_if(deque.begin(), deque.end(), [&](const InQueueItem<O, I> &item) {
                return std::get<0>(item) == args;
            }),
            deque.end());
    }

    bool replase(const I &old_args, InQueueFunction<O, I> new_function, PopCallback<O, I> new_callback)
    {
        std::unique_lock<std::mutex> mlock(Queue<InQueueItem<O, I>>::m_mutex);
        auto &deque = Queue<InQueueItem<O, I>>::m_deque;

        auto it = std::find_if(deque.begin(), deque.end(), [&](const InQueueItem<O, I> &item) {
            return std::get<0>(item) == old_args;
        });

        if (it != deque.end())
        {
            std::get<1>(*it) = new_function;
            std::get<2>(*it) = new_callback;
            return true;
        }
        return false;
    }

    void setGlobalPopCallback(PopCallback<O, I> callback)
    {
        m_globalPopCallback = callback;
    }

    void setPause(unsigned long millis)
    {
        m_PauseMillis = millis;
    }

  private:
    void _threadMain()
    {
        while (m_alive.test_and_set())
        {
            InQueueItem<O, I> item;
            if (Queue<InQueueItem<O, I>>::pop(item))
            {
                auto result = std::move(std::get<1>(item)(std::get<0>(item)));
                auto pop_callback = std::get<2>(item);

                if (pop_callback)
                {
                    pop_callback(std::get<0>(item), result);
                }
                if (m_globalPopCallback)
                {
                    m_globalPopCallback(std::get<0>(item), result);
                }
            }

            if (m_PauseMillis)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_PauseMillis));
            }
        }
    }

    std::atomic_flag m_alive = {1};
    std::thread m_thread;
    PopCallback<O, I> m_globalPopCallback;
    std::atomic<unsigned long> m_PauseMillis;

    std::atomic_bool m_pushLock;
};