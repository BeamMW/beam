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

                m_trezor->callback_Failure([&](const Message &msg, std::string session, size_t queue_size)
                {
                    // !TODO: handle errors here
                    LOG_ERROR() << "FAIL REASON: " << child_cast<Message, Failure>(msg).message();
                });

                m_trezor->callback_Success([&](const Message &msg, std::string session, size_t queue_size)
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

                m_trezor->call_BeamGetOwnerKey(true, [&m_runningFlag, &result](const Message &msg, std::string session, size_t queue_size)
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

                m_trezor->call_BeamGenerateNonce(slot, [&m_runningFlag, &result](const Message &msg, std::string session, size_t queue_size)
                {
                    result.m_X = beam::Blob(child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).x().c_str(), 32);
                    result.m_Y = child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).y();
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

        void getNoncePublic(uint8_t slot, HWWallet::Result<ECC::Point> callback)
        {
            if (m_trezor)
            {
                std::atomic_flag m_runningFlag;
                m_runningFlag.test_and_set();
                ECC::Point result;

                m_trezor->call_BeamGetNoncePublic(slot, [&m_runningFlag, &result](const Message& msg, std::string session, size_t queue_size)
                {
                    result.m_X = beam::Blob(child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).x().c_str(), 32);
                    result.m_Y = child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).y();
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

        void generateKey(const ECC::Key::IDV& idv, bool isCoinKey, HWWallet::Result<ECC::Point> callback)
        {
            if (m_trezor)
            {
                std::atomic_flag m_runningFlag;
                m_runningFlag.test_and_set();
                ECC::Point result;

                m_trezor->call_BeamGenerateKey(idv.m_Idx, idv.m_Type, idv.m_SubIdx, idv.m_Value, isCoinKey, [&m_runningFlag, &result](const Message &msg, std::string session, size_t queue_size)
                {
                    result.m_X = beam::Blob(child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).x().c_str(), 32);
                    result.m_Y = child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).y();
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

        void generateRangeproof(const ECC::Key::IDV& idv, bool isCoinKey, HWWallet::Result<ECC::RangeProof::Confidential> callback)
        {
            if (m_trezor)
            {
                std::atomic_flag m_runningFlag;
                m_runningFlag.test_and_set();
                ECC::RangeProof::Confidential result;

                m_trezor->call_BeamGenerateRangeproof(idv.m_Idx, idv.m_Type, idv.m_SubIdx, idv.m_Value, isCoinKey, [&m_runningFlag, &result](const Message& msg, std::string session, size_t queue_size)
                {
                    const uint8_t* rp_raw = reinterpret_cast<const uint8_t*>(child_cast<Message, hw::trezor::messages::beam::BeamRangeproofData>(msg).data().c_str());
                    rangeproof_confidential_t rp;
                    memcpy(&rp, rp_raw, sizeof(rangeproof_confidential_t));

                    {
                        result.m_Part1.m_A.m_X = beam::Blob(rp.part1.a.x, 32);
                        result.m_Part1.m_A.m_Y = rp.part1.a.y;

                        result.m_Part1.m_S.m_X = beam::Blob(rp.part1.s.x, 32);
                        result.m_Part1.m_S.m_Y = rp.part1.s.y;
                    }

                    {
                        result.m_Part2.m_T1.m_X = beam::Blob(rp.part2.t1.x, 32);
                        result.m_Part2.m_T1.m_Y = rp.part2.t1.y;

                        result.m_Part2.m_T2.m_X = beam::Blob(rp.part2.t2.x, 32);
                        result.m_Part2.m_T2.m_Y = rp.part2.t2.y;
                    }

                    {
                        result.m_P_Tag.m_pCondensed[0].m_Value = beam::Blob(rp.p_tag.condensed[0].d, 32);
                        result.m_P_Tag.m_pCondensed[1].m_Value = beam::Blob(rp.p_tag.condensed[1].d, 32);
                    }

                    for (int c = 0; c < INNER_PRODUCT_N_CYCLES; c++)
                    {
                        for (int i = 0; i < 2; i++)
                        {
                            result.m_P_Tag.m_pLR[c][i].m_X = beam::Blob(rp.p_tag.LR[c][i].x, 32);
                            result.m_P_Tag.m_pLR[c][i].m_Y = rp.p_tag.LR[c][i].y;
                        }
                    }

                    {
                        result.m_Mu.m_Value = beam::Blob(rp.mu.d, sizeof rp.mu);
                        result.m_tDot.m_Value = beam::Blob(rp.tDot.d, 32);
                        result.m_Part3.m_TauX.m_Value = beam::Blob(rp.part3.tauX.d, 32);
                    }

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

    void HWWallet::getNoncePublic(uint8_t slot, Result<ECC::Point> callback) const
    {
        m_impl->getNoncePublic(slot, callback);
    }

    void HWWallet::generateKey(const ECC::Key::IDV& idv, bool isCoinKey, Result<ECC::Point> callback) const
    {
        m_impl->generateKey(idv, isCoinKey, callback);
    }

    void HWWallet::generateRangeProof(const ECC::Key::IDV& idv, bool isCoinKey, Result<ECC::RangeProof::Confidential> callback) const
    {
        m_impl->generateRangeproof(idv, isCoinKey, callback);
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

    ECC::Point HWWallet::getNoncePublicSync(uint8_t slot) const
    {
        ECC::Point result;

        getNoncePublic(slot, [&result](const ECC::Point& nonce)
        {
            result = nonce;
        });

        return result;
    }

    ECC::Point HWWallet::generateKeySync(const ECC::Key::IDV& idv, bool isCoinKey) const
    {
        ECC::Point result;

        generateKey(idv, isCoinKey, [&result](const ECC::Point& key)
        {
            result = key;
        });

        return result;
    }

    ECC::RangeProof::Confidential HWWallet::generateRangeProofSync(const ECC::Key::IDV& idv, bool isCoinKey) const
    {
        ECC::RangeProof::Confidential result;

        generateRangeProof(idv, isCoinKey, [&result](const ECC::RangeProof::Confidential& key)
        {
            result = key;
        });

        return result;
    }
}
