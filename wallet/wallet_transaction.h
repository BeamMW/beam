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

#include "wallet/common.h"
#include "wallet/wallet_db.h"

#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam { namespace wallet
{
    struct ITransaction
    {
        using Ptr = std::shared_ptr<ITransaction>;
        virtual TxType getType() const = 0;
        virtual void update() = 0;
        virtual void cancel() = 0;
    };

    //
    // State machine for managing per transaction negotiations between wallets
    // 
    class BaseTransaction : public ITransaction
    {
    public:
        using Ptr = std::shared_ptr<BaseTransaction>;
        BaseTransaction(INegotiatorGateway& gateway
            , beam::IKeyChain::Ptr keychain
            , const TxID& txID);

        const TxID& getTxID() const;
        void cancel() override;

        template <typename T>
        bool getParameter(TxParameterID paramID, T& value) const
        {
            ByteBuffer b;
            if (m_keychain->getTxParameter(getTxID(), static_cast<uint32_t>(paramID), b))
            {
                if (!b.empty())
                {
                    Deserializer d;
                    d.reset(b.data(), b.size());
                    d & value;
                }
                else
                {
                    ZeroObject(value);
                }
                return true;
            }
            return false;
        }

        template <typename T>
        bool setParameter(TxParameterID paramID, const T& value)
        {
            return m_keychain->setTxParameter(getTxID(), static_cast<uint32_t>(paramID), toByteBuffer(value));
        }
        
        bool getParameter(TxParameterID paramID, ECC::Point::Native& value) const;
        bool getParameter(TxParameterID paramID, ECC::Scalar::Native& value) const;

        bool setParameter(TxParameterID paramID, const ECC::Point::Native& value);
        bool setParameter(TxParameterID paramID, const ECC::Scalar::Native& value);

        bool setParameter(TxParameterID paramID, const ByteBuffer& value);

    protected:
 
        void confirmKernel(const TxKernel& kernel);
        void completeTx();
        void rollbackTx();
        void updateTxDescription(TxDescription::Status s);
        TxKernel::Ptr createKernel(Amount fee, Height minHeight) const;
        ECC::Signature::MultiSig createMultiSig(const TxKernel& kernel, const ECC::Scalar::Native& blindingExcess) const;
        std::vector<Input::Ptr> getTxInputs(const TxID& txID) const;
        std::vector<Output::Ptr> getTxOutputs(const TxID& txID) const;
        std::vector<Coin> getUnconfirmedOutputs() const;

        void onFailed(bool notify = false);

        bool getTip(Block::SystemState::Full& state) const;

        template <typename T>
        ByteBuffer toByteBuffer(const T& value) const
        {
            Serializer s;
            s & value;
            ByteBuffer b;
            s.swap_buf(b);
            return b;
        }

        ByteBuffer toByteBuffer(const ECC::Point::Native& value) const;
        ByteBuffer toByteBuffer(const ECC::Scalar::Native& value) const;

        template <typename T>
        void addParameter(SetTxParameter& msg, TxParameterID paramID, T&& value) const
        {
            msg.m_Parameters.emplace_back(paramID, toByteBuffer(value));
        }

        bool sendTxParameters(SetTxParameter&& msg) const;

    protected:

        INegotiatorGateway& m_gateway;
        beam::IKeyChain::Ptr m_keychain;

        TxID m_txID;
        TxDescription m_txDesc;
    };

    class SimpleTransaction : public BaseTransaction
    {
    public:
        SimpleTransaction(INegotiatorGateway& gateway
            , beam::IKeyChain::Ptr keychain
            , const TxID& txID);
        void update() override;
    private:
        TxType getType() const override;

        void sendConfirmTransaction(const ECC::Scalar& peerSignature) const;
        void sendConfirmInvitation(const ECC::Point::Native& publicExcess, const ECC::Signature& partialSignature) const;
    };
}}
