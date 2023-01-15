// Copyright 2019 The Beam Team
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

#include "remote_key_keeper.h"
#include "../utility/thread.h"
#include "../utility/containers.h"

namespace beam::wallet
{
    struct HidInfo
    {
        struct Entry
        {
            std::string m_sPath;
            std::string m_sManufacturer;
            std::string m_sProduct;

            uint16_t m_Vendor;
            uint16_t m_Product;
            uint16_t m_Version;
        };

        static std::vector<Entry> Enum(uint16_t nVendor);
    };

    struct UsbIO
    {
#ifdef WIN32
        HANDLE m_hFile;
        HANDLE m_hEvent;

        static void EnsureIoPending();
        void WaitSync();

#else // WIN32
        int m_hFile;
#endif // WIN32

        struct Frame;

        void WriteFrame(const uint8_t*, uint16_t);
        uint16_t ReadFrame(uint8_t*, uint16_t);

        void Write(const void*, uint16_t);
        uint16_t Read(void*, uint16_t);

        UsbIO();
        ~UsbIO();

        void Open(const char* szPath); // throws exc on error
    };

    class UsbKeyKeeper
        :public RemoteKeyKeeper
    {
    public:

        struct CallStats
        {
            uint32_t m_nRequest;
            uint32_t m_nResponse;

            // dbg info returned by device
            struct
            {
                uint8_t m_OpCode;
                uint8_t m_Major;
                uint8_t m_Minor;
            } m_Dbg;
        };

    private:

        struct Task
            :public boost::intrusive::list_base_hook<>
            ,public CallStats
        {
            typedef std::unique_ptr<Task> Ptr;

            uint8_t* m_pBuf;
            Handler::Ptr m_pHandler;
            Status::Type m_eRes; // set after processing
        };

        struct TaskList :public intrusive::list_autoclear<Task>
        {
            std::mutex m_Mutex;

            bool Push(Task::Ptr&); // returns true if list was empty
            bool PushLocked(Task::Ptr&);
            bool Pop(Task::Ptr&);
            bool PopLocked(Task::Ptr&);
        };

        struct Event
        {
#ifdef WIN32
            typedef  HANDLE Handle;
#else // WIN32
            typedef int Handle;
            Handle m_hSetter;
#endif // WIN32
            Handle m_hEvt;

            Event();
            ~Event();

            void Set();
            void Create();
        };

        struct ShutdownExc {};

        TaskList m_lstPending;
        TaskList m_lstDone;

        Event m_evtShutdown;
        Event m_evtTask;
        MyThread m_Thread; // theoretically it's possible to handle async read events in the same thread, unfortunately it's too complex to integrate with uv

        io::AsyncEvent::Ptr m_pEvt;

        void SendRequestAsync(void* pBuf, uint32_t nRequest, uint32_t nResponse, const Handler::Ptr& pHandler) override;

        void RunThread();
        void RunThreadGuarded();
        bool WaitEvent(const Event::Handle*, const uint32_t* pTimeout_ms);
        void OnEvent();

    public:

        enum struct DevState {
            Disconnected,
            Connected,
            Stalled,
        };

        static std::shared_ptr<UsbKeyKeeper> Open(const std::string& sPath);

        std::string m_sPath; // don't modify after start

        struct IEvents
        {
            virtual void OnDevState(const std::string& sErr, DevState) {} // is there an error, or device stalled (perhaps waiting for the user interaction)
            virtual void OnDevReject(const CallStats&) {}
        };

        IEvents* m_pEvents = nullptr;


        void StartSafe();
        void Stop();

        virtual ~UsbKeyKeeper();

    private:

        DevState m_State = DevState::Disconnected;
        bool m_NotifyStatePending = false;
        std::string m_sLastError;
        void NotifyState(std::string* pErr, DevState);
    };

    struct UsbKeyKeeper_ToConsole
        :public UsbKeyKeeper
    {
        static thread_local UsbKeyKeeper::IEvents* s_pEvents;

        struct Events
            :public UsbKeyKeeper::IEvents
        {
            void OnDevState(const std::string& sErr, DevState) override;
            void OnDevReject(const CallStats&) override;
        } m_Events;

        UsbKeyKeeper_ToConsole()
        {
            m_pEvents = &m_Events;
        }
    };
}


