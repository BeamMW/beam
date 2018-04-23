#pragma once

#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>

namespace beam
{
    using Uuid = std::array<uint8_t, 16>;
    namespace wallet
    {
        namespace msm = boost::msm;
        namespace msmf = boost::msm::front;
        namespace mpl = boost::mpl;

        template <typename Derived>
        class FSMHelper 
        {
        public:
            void start()
            {
                static_cast<Derived*>(this)->m_fsm.start();
            }

            template<typename Event>
            bool processEvent(const Event& event)
            {
                return static_cast<Derived*>(this)->m_fsm.process_event(event) == msm::back::HANDLED_TRUE;
            }

            template<typename Event>
            void enqueueEvent(const Event& event)
            {
                static_cast<Derived*>(this)->m_fsm.enqueue_event(event);
            }

            void executeQueuedEvents()
            {
                if (!static_cast<Derived*>(this)->m_fsm.get_message_queue().empty())
                {
                    static_cast<Derived*>(this)->m_fsm.execute_queued_events();
                }
            }
        };
    }
}