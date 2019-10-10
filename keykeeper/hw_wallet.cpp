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
    HWWallet::HWWallet(OnError onError)
        : m_client(std::make_shared<Client>())   
    {
        //auto enumerates = m_client->enumerate();

        //if (enumerates.empty())
        //{
        //    LOG_INFO() << "there is no Trezor device connected";
        //    return;
        //}

        //auto& enumerate = enumerates.front();

        //if (enumerate.session != "null")
        //{
        //    m_client->release(enumerate.session);
        //    enumerate.session = "null";
        //}

        //m_trezor = std::make_shared<DeviceManager>();

        //m_trezor->callback_Failure([&](const Message& msg, std::string session, size_t queue_size)
        //{
        //    auto message = child_cast<Message, Failure>(msg).message();

        //    onError(message);

        //    LOG_ERROR() << "FAIL REASON: " << message;
        //});

        //m_trezor->callback_Success([&](const Message& msg, std::string session, size_t queue_size)
        //{
        //    LOG_INFO() << "SUCCESS: " << child_cast<Message, Success>(msg).message();
        //});

        //try
        //{
        //    m_trezor->init(enumerate);
        //}
        //catch (std::runtime_error e)
        //{
        //    LOG_ERROR() << e.what();
        //}
    }

    bool HWWallet::isConnected() const
    {
        return !m_client->enumerate().empty();
    }

    std::vector<std::string> HWWallet::getDevices() const
    {
        auto devices = m_client->enumerate();
        std::vector<std::string> items;

        if (devices.empty())
            return items;

        for (auto device : devices)
        {
            items.push_back("Trezor(device.path=" + device.path
                + ",product=" + std::to_string(device.product)
                + ",session=" + device.session
                + ",vendor=" + std::to_string(device.vendor)
                + ")");
        }

        return items;
    }

    std::shared_ptr<DeviceManager> getTrezor(std::shared_ptr<Client> client)
    {
        auto enumerates = client->enumerate();
        auto& enumerate = enumerates.front();

        if (enumerate.session != "null")
        {
            client->release(enumerate.session);
            enumerate.session = "null";
        }

        auto trezor = std::make_shared<DeviceManager>();

        trezor->callback_Failure([&](const Message& msg, std::string session, size_t queue_size)
        {
            auto message = child_cast<Message, Failure>(msg).message();

            //onError(message);

            LOG_ERROR() << "TREZOR FAIL REASON: " << message;
        });

        trezor->callback_Success([&](const Message& msg, std::string session, size_t queue_size)
        {
            LOG_INFO() << "TREZOR SUCCESS: " << child_cast<Message, Success>(msg).message();
        });

        try
        {
            trezor->init(enumerate);
        }
        catch (std::runtime_error e)
        {
            LOG_ERROR() << e.what();
        }

        return trezor;
    }

    void HWWallet::getOwnerKey(Result<std::string> callback) const
    {
        assert(isConnected());

        std::atomic_flag m_runningFlag;
        m_runningFlag.test_and_set();
        std::string result;

        auto trezor = getTrezor(m_client);

        trezor->call_BeamGetOwnerKey(true, [&m_runningFlag, &result](const Message& msg, std::string session, size_t queue_size)
        {
            result = child_cast<Message, hw::trezor::messages::beam::BeamOwnerKey>(msg).key();
            m_runningFlag.clear();
        });

        while (m_runningFlag.test_and_set());

        callback(result);
    }

    void HWWallet::generateNonce(uint8_t slot, Result<ECC::Point> callback) const
    {
        assert(isConnected());

        std::atomic_flag m_runningFlag;
        m_runningFlag.test_and_set();
        ECC::Point result;

        auto trezor = getTrezor(m_client);

        trezor->call_BeamGenerateNonce(slot, [&m_runningFlag, &result](const Message& msg, std::string session, size_t queue_size)
            {
                result.m_X = beam::Blob(child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).x().c_str(), 32);
                result.m_Y = child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).y();
                m_runningFlag.clear();
            });

        while (m_runningFlag.test_and_set());

        callback(result);
    }

    void HWWallet::getNoncePublic(uint8_t slot, Result<ECC::Point> callback) const
    {
        assert(isConnected());

        std::atomic_flag m_runningFlag;
        m_runningFlag.test_and_set();
        ECC::Point result;

        auto trezor = getTrezor(m_client);

        trezor->call_BeamGetNoncePublic(slot, [&m_runningFlag, &result](const Message& msg, std::string session, size_t queue_size)
            {
                result.m_X = beam::Blob(child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).x().c_str(), 32);
                result.m_Y = child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).y();
                m_runningFlag.clear();
            });

        while (m_runningFlag.test_and_set());

        callback(result);
    }

    void HWWallet::generateKey(const ECC::Key::IDV& idv, bool isCoinKey, Result<ECC::Point> callback) const
    {
        assert(isConnected());

        std::atomic_flag m_runningFlag;
        m_runningFlag.test_and_set();
        ECC::Point result;

        auto trezor = getTrezor(m_client);

        trezor->call_BeamGenerateKey(idv.m_Idx, idv.m_Type, idv.m_SubIdx, idv.m_Value, isCoinKey, [&m_runningFlag, &result](const Message& msg, std::string session, size_t queue_size)
            {
                result.m_X = beam::Blob(child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).x().c_str(), 32);
                result.m_Y = child_cast<Message, hw::trezor::messages::beam::BeamECCPoint>(msg).y();
                m_runningFlag.clear();
            });

        while (m_runningFlag.test_and_set());

        callback(result);
    }

    void HWWallet::generateRangeProof(const ECC::Key::IDV& idv, bool isCoinKey, Result<ECC::RangeProof::Confidential> callback) const
    {
        assert(isConnected());

        std::atomic_flag m_runningFlag;
        m_runningFlag.test_and_set();
        ECC::RangeProof::Confidential result;

        auto trezor = getTrezor(m_client);

        trezor->call_BeamGenerateRangeproof(idv.m_Idx, idv.m_Type, idv.m_SubIdx, idv.m_Value, isCoinKey, [&m_runningFlag, &result](const Message& msg, std::string session, size_t queue_size)
            {
                const uint8_t* rp_raw = reinterpret_cast<const uint8_t*>(child_cast<Message, hw::trezor::messages::beam::BeamRangeproofData>(msg).data().c_str());
                rangeproof_confidential_packed_t rp;
                memcpy(&rp, rp_raw, sizeof(rangeproof_confidential_packed_t));

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
                    result.m_P_Tag.m_pCondensed[0].m_Value = beam::Blob(rp.p_tag.condensed[0], 32);
                    result.m_P_Tag.m_pCondensed[1].m_Value = beam::Blob(rp.p_tag.condensed[1], 32);
                }

                for (unsigned int c = 0; c < INNER_PRODUCT_N_CYCLES; c++)
                {
                    for (int i = 0; i < 2; i++)
                    {
                        result.m_P_Tag.m_pLR[c][i].m_X = beam::Blob(rp.p_tag.LR[c][i].x, 32);
                        result.m_P_Tag.m_pLR[c][i].m_Y = rp.p_tag.LR[c][i].y;
                    }
                }

                {
                    result.m_Mu.m_Value = beam::Blob(rp.mu, sizeof rp.mu);
                    result.m_tDot.m_Value = beam::Blob(rp.tDot, 32);
                    result.m_Part3.m_TauX.m_Value = beam::Blob(rp.part3.tauX, 32);
                }

                m_runningFlag.clear();
            });

        while (m_runningFlag.test_and_set());

        callback(result);
    }

    void HWWallet::signTransaction(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const TxData& tx, Result<ECC::Scalar> callback) const
    {
        assert(isConnected());

        std::atomic_flag m_runningFlag;
        m_runningFlag.test_and_set();
        ECC::Scalar result;

        std::vector<key_idv_t> _inputs;
        _inputs.reserve(inputs.size());

        for (const auto& from : inputs)
        {
            key_idv_t& to = _inputs.emplace_back();
            to.idx = from.m_Idx;
            to.sub_idx = from.m_SubIdx;
            to.type = from.m_Type;
            to.value = from.m_Value;
        }

        std::vector<key_idv_t> _outputs;
        _outputs.reserve(outputs.size());

        for (const auto& from : outputs)
        {
            key_idv_t& to = _outputs.emplace_back();
            to.idx = from.m_Idx;
            to.sub_idx = from.m_SubIdx;
            to.type = from.m_Type;
            to.value = from.m_Value;
        }

        transaction_data_t _tx;
        _tx.fee = tx.fee;
        _tx.min_height = tx.height.m_Min;
        _tx.max_height = tx.height.m_Max;

        std::memcpy(_tx.kernel_commitment.x, tx.kernelCommitment.m_X.m_pData, 32);
        _tx.kernel_commitment.y = tx.kernelCommitment.m_Y;

        std::memcpy(_tx.kernel_nonce.x, tx.kernelNonce.m_X.m_pData, 32);
        _tx.kernel_nonce.y = tx.kernelNonce.m_Y;

        _tx.nonce_slot = tx.nonceSlot;
        std::memcpy(_tx.offset, tx.offset.m_Value.m_pData, 32);

        auto trezor = getTrezor(m_client);

        trezor->call_BeamSignTransaction(_inputs, _outputs, _tx, [&m_runningFlag, &result](const Message& msg, std::string session, size_t queue_size)
            {
                result.m_Value = beam::Blob(child_cast<Message, hw::trezor::messages::beam::BeamSignedTransaction>(msg).signature().c_str(), 32);

                m_runningFlag.clear();
            });

        while (m_runningFlag.test_and_set());

        callback(result);
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

    ECC::Scalar HWWallet::signTransactionSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const TxData& tx) const
    {
        ECC::Scalar result;

        signTransaction(inputs, outputs, tx, [&result](const ECC::Scalar& key)
        {
            result = key;
        });

        return result;
    }
}
