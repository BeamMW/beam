#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <tuple>
#include "queue.h"
#include "models/call.hpp"

template <typename OUT, typename IN>
using InQueueFunction = std::function<OUT(const IN &)>;
template <typename OUT, typename IN>
using PopCallback = std::function<void(const IN &, const OUT &)>;
template <typename OUT, typename IN>
using InQueueItem = std::tuple<IN, InQueueFunction<OUT, IN>, PopCallback<OUT, IN>>;

template <typename OUT, typename IN>
class WorkingQueue : public Queue<InQueueItem<OUT, IN>>
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

    void push(const IN &args, InQueueFunction<OUT, IN> function, PopCallback<OUT, IN> callback = nullptr)
    {
        if (!m_pushLock)
        {
            if (!replase(args, function, callback))
            {
                Queue<InQueueItem<OUT, IN>>::push(std::make_tuple(args, function, callback));
            }
        }
    }

    void remove(const IN &args)
    {
        std::unique_lock<std::mutex> mlock(Queue<InQueueItem<OUT, IN>>::m_mutex);

        auto &deque = Queue<InQueueItem<OUT, IN>>::m_deque;
        deque.erase(
            std::remove_if(deque.begin(), deque.end(), [&](const InQueueItem<OUT, IN> &item) {
                return std::get<0>(item) == args;
            }),
            deque.end());
    }

    bool replase(const IN &old_args, InQueueFunction<OUT, IN> new_function, PopCallback<OUT, IN> new_callback)
    {
        std::unique_lock<std::mutex> mlock(Queue<InQueueItem<OUT, IN>>::m_mutex);
        auto &deque = Queue<InQueueItem<OUT, IN>>::m_deque;

        auto it = std::find_if(deque.begin(), deque.end(), [&](const InQueueItem<OUT, IN> &item) {
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

    void setGlobalPopCallback(PopCallback<OUT, IN> callback)
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
            InQueueItem<OUT, IN> item;
            if (Queue<InQueueItem<OUT, IN>>::pop(item))
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
    PopCallback<OUT, IN> m_globalPopCallback;
    std::atomic<unsigned long> m_PauseMillis;

    std::atomic_bool m_pushLock;
};