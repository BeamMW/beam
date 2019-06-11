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

#include "hw_wallet.h"
#include "utility/logger.h"
#include "utility/helpers.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#else
#	pragma warning (push, 0)
#   pragma warning( disable : 4996 )
#endif

#include "client.hpp"
#include "queue/working_queue.h"
#include "device_manager.hpp"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (pop)
#endif

namespace beam
{
    class HWWalletImpl
    {
    public:
        HWWalletImpl()
        {
            auto enumerates = m_client.enumerate();

            if (enumerates.size() == 0)
            {
                LOG_INFO() << "there is no device connected";
                return;
            }

            auto& enumerate = enumerates.front();

            {
                m_trezor = std::make_unique<DeviceManager>();

                if (enumerate.session != "null")
                {
                    m_client.release(enumerate.session);
                    enumerate.session = "null";
                }

                m_trezor->callback_Failure([&](const Message &msg, size_t queue_size) 
                {
                    // !TODO: handle errors here
                    LOG_ERROR() << "FAIL REASON: " << child_cast<Message, Failure>(msg).message();
                });

                m_trezor->callback_Success([&](const Message &msg, size_t queue_size) 
                {
                    LOG_INFO() << "SUCCESS: " << child_cast<Message, Success>(msg).message();
                });

                try
                {
                    m_trezor->init(enumerate);
                    //m_trezor->call_Ping("hello beam", true);
                }
                catch (std::runtime_error e)
                {
                    LOG_ERROR() << e.what();
                }
            }
        }

        ~HWWalletImpl()
        {
            
        }

        void getOwnerKey(HWWallet::Result<std::string> callback)
        {
            if (m_trezor)
            {
                std::atomic_flag m_runningFlag;
                m_runningFlag.test_and_set();
                std::string result;

                m_trezor->call_BeamGetOwnerKey(true, [&m_runningFlag, &result](const Message &msg, size_t queue_size)
                {
                    result = child_cast<Message, hw::trezor::messages::beam::BeamOwnerKey>(msg).key();
                    m_runningFlag.clear();
                });

                while (m_runningFlag.test_and_set());

                callback(result);
            }
            else
            {
                LOG_ERROR() << "HW wallet not initialized";
            }
        }

        void generateNonce(uint8_t slot, HWWallet::Result<ECC::Point> callback)
        {
            if (m_trezor)
            {
                std::atomic_flag m_runningFlag;
                m_runningFlag.test_and_set();
                ECC::Point result;

                m_trezor->call_BeamGenerateNonce(slot, [&m_runningFlag, &result](const Message &msg, size_t queue_size)
                {
                    result.m_X = beam::Blob(child_cast<Message, hw::trezor::messages::beam::BeamECCImage>(msg).image_x().c_str(), 32);
                    result.m_Y = 0;
                    m_runningFlag.clear();
                });

                while (m_runningFlag.test_and_set());

                callback(result);
            }
            else
            {
                LOG_ERROR() << "HW wallet not initialized";
            }
        }

        void generateKey(const ECC::Key::IDV& idv, bool isCoinKey, HWWallet::Result<std::string> callback)
        {
            if (m_trezor)
            {
                std::atomic_flag m_runningFlag;
                m_runningFlag.test_and_set();
                std::string result;

                m_trezor->call_BeamGenerateKey(idv.m_Idx, idv.m_Type, idv.m_SubIdx, idv.m_Value, isCoinKey, [&m_runningFlag, &result](const Message &msg, size_t queue_size)
                {
                    result = to_hex(reinterpret_cast<const uint8_t*>(child_cast<Message, hw::trezor::messages::beam::BeamPublicKey>(msg).pub_x().c_str()), 32);
                    m_runningFlag.clear();
                });

                while (m_runningFlag.test_and_set());

                callback(result);
            }
            else
            {
                LOG_ERROR() << "HW wallet not initialized";
            }
        }

    private:

        Client m_client;
        std::unique_ptr<DeviceManager> m_trezor;

    };

    HWWallet::HWWallet() 
        : m_impl(std::make_shared<HWWalletImpl>()) {}

    void HWWallet::getOwnerKey(Result<std::string> callback) const
    {
        m_impl->getOwnerKey(callback);
    }

    void HWWallet::generateNonce(uint8_t slot, Result<ECC::Point> callback) const
    {
        m_impl->generateNonce(slot, callback);
    }

    void HWWallet::generateKey(const ECC::Key::IDV& idv, bool isCoinKey, Result<std::string> callback) const
    {
        m_impl->generateKey(idv, isCoinKey, callback);
    }

    std::string HWWallet::getOwnerKeySync() const
    {
        std::string result;

        getOwnerKey([&result](const std::string& key)
        {
            result = key;
        });

        return result;
    }

    ECC::Point HWWallet::generateNonceSync(uint8_t slot) const
    {
        ECC::Point result;

        generateNonce(slot, [&result](const ECC::Point& nonce)
        {
            result = nonce;
        });

        return result;
    }

    std::string HWWallet::generateKeySync(const ECC::Key::IDV& idv, bool isCoinKey) const
    {
        std::string result;

        generateKey(idv, isCoinKey, [&result](const std::string& key)
        {
            result = key;
        });

        return result;
    }

}
