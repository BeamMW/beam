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

#include "common.h"
#include "wallet_db.h"
#include "base_transaction.h"

#include <condition_variable>
#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam::wallet
{

    class BaseTxBuilder : public std::enable_shared_from_this<BaseTxBuilder>
    {
    public:
        BaseTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amount, Amount fee);
        virtual ~BaseTxBuilder() = default;
        void SelectInputs();
        void AddChange();
        void GenerateAssetCoin(Amount amount, bool change);
        void GenerateBeamCoin(Amount amount, bool change);
        bool CreateOutputs();
        bool FinalizeOutputs();
        bool LoadKernel();
        bool HasKernelID() const;
        void CreateKernel();
        void GenerateNonce();
        virtual ECC::Point::Native GetPublicExcess() const;
        ECC::Point::Native GetPublicNonce() const;
        Asset::ID GetAssetId() const;
        bool GetInitialTxParams();
        bool GetInputs();
        bool GetOutputs();
        bool GetPeerPublicExcessAndNonce();
        bool GetPeerSignature();
        bool GetPeerInputsAndOutputs();
        void FinalizeSignature();
        bool CreateInputs();
        void FinalizeInputs();
        virtual Transaction::Ptr CreateTransaction();
        bool SignSender(bool initial);
        bool SignReceiver();
        bool IsPeerSignatureValid() const;

        Amount GetAmount() const;
        const AmountList& GetAmountList() const;
        Amount GetFee() const;
        Height GetLifetime() const;
        Height GetMinHeight() const;
        virtual Height GetMaxHeight() const;
        const std::vector<Input::Ptr>& GetInputs() const;
        const std::vector<Output::Ptr>& GetOutputs() const;
        const ECC::Scalar::Native& GetOffset() const;
        const ECC::Scalar::Native& GetPartialSignature() const;
        const TxKernel& GetKernel() const;
        const Merkle::Hash& GetKernelID() const;
        void StoreKernelID();
        std::string GetKernelIDString() const;
        bool UpdateMaxHeight();
        bool IsAcceptableMaxHeight() const;
        ECC::Hash::Value GetLockImage() const;
        SubTxID GetSubTxID() const;

        const std::vector<Coin::ID>& GetInputCoins() const;
        const std::vector<Coin::ID>& GetOutputCoins() const;

    protected:


        template <typename Result, typename Func, typename ContinueFunc>
        void DoAsync(Func&& asyncFunc, ContinueFunc&& continueFunc, int line)
        {
            if (auto it = m_Exceptions.find(line); it != m_Exceptions.end())
            {
                auto ex = it->second;
                m_Exceptions.erase(it);
                std::rethrow_exception(ex);
            }
            auto thisHolder = shared_from_this();
            auto txHolder = m_Tx.shared_from_this(); // increment use counter of tx object. We use it to avoid tx object desctruction during Update call.
            m_Tx.GetAsyncAcontext().OnAsyncStarted();

            asyncFunc(
                [thisHolder, this, txHolder, continueFunc](Result&& res)
                {
                    continueFunc(std::move(res));
                    m_Tx.UpdateAsync(); // may complete transaction
                    m_Tx.GetAsyncAcontext().OnAsyncFinished();
                },
                [thisHolder, this, line, txHolder](std::exception_ptr ex)
                {
                    m_Exceptions.emplace(line, ex);
                    m_Tx.UpdateAsync();
                    m_Tx.GetAsyncAcontext().OnAsyncFinished();
                });
        }

        std::map<int, std::exception_ptr> m_Exceptions;

    private:
        Amount GetMinimumFee() const;
        void CheckMinimumFee();

        template<typename T1, typename T2>
        void StoreAndLoad(TxParameterID parameterID, const T1& source, T2& dest)
        {
            m_Tx.SetParameter(parameterID, source, m_SubTxID);
            m_Tx.GetParameter(parameterID, dest, m_SubTxID);
        }
    protected:
        BaseTransaction& m_Tx;
        SubTxID m_SubTxID;

        // input
        Asset::ID m_AssetId;
        AmountList m_AmountList;
        Amount m_Fee;
        Amount m_ChangeBeam;
        Amount m_ChangeAsset;
        Height m_Lifetime;
        Height m_MinHeight;
        Height m_MaxHeight;
        std::vector<Input::Ptr> m_Inputs;
        std::vector<Output::Ptr> m_Outputs;
        ECC::Scalar::Native m_Offset; // goes to offset

        std::vector<Coin::ID> m_InputCoins;
        std::vector<Coin::ID> m_OutputCoins;
        size_t m_NonceSlot = 0;
        ECC::Point::Native m_PublicNonce;
        ECC::Point::Native m_PublicExcess;

        // peer values
        ECC::Scalar::Native m_PartialSignature;
        ECC::Point::Native m_PeerPublicNonce;
        ECC::Point::Native m_PeerPublicExcess;
        std::vector<Input::Ptr> m_PeerInputs;
        std::vector<Output::Ptr> m_PeerOutputs;
        ECC::Scalar::Native m_PeerOffset;
        Height m_PeerMaxHeight;

        // deduced values,
        TxKernelStd::Ptr m_Kernel;
        ECC::Scalar::Native m_PeerSignature;

        mutable boost::optional<Merkle::Hash> m_KernelID;
        io::AsyncEvent::Ptr m_AsyncCompletedEvent;
    };
}
