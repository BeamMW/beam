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

#include <condition_variable>
#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam::wallet
{
    class BaseTransaction;

    class BaseTxBuilder
    {
    public:
        BaseTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amount, Amount fee);

        void SelectInputs();
        void AddChangeOutput();
        virtual void AddOutput(Amount amount, bool bChange);
        bool FinalizeOutputs();
        Output::Ptr CreateOutput(Amount amount, bool bChange);
        void CreateKernel();
        void FinalizeSignature();
        virtual Transaction::Ptr CreateTransaction();
        void SignPartial();
        void StoreKernelID();
        bool IsPeerSignatureValid() const;

        ECC::Point::Native GetPublicExcess() const;
        ECC::Point::Native GetPublicNonce() const;
        virtual bool GetInitialTxParams();
        bool GetPeerPublicExcessAndNonce();
        bool GetPeerSignature();
        bool GetPeerInputsAndOutputs();
        Amount GetAmount() const;
        const AmountList& GetAmountList() const;
        Amount GetFee() const;
        Height GetMinHeight() const;
        Height GetMaxHeight() const;
        const std::vector<Input::Ptr>& GetInputs() const;
        const std::vector<Output::Ptr>& GetOutputs() const;
        const ECC::Scalar::Native& GetOffset() const;
        const ECC::Scalar::Native& GetPartialSignature() const;
        const TxKernel& GetKernel() const;
        std::string GetKernelIDString() const;
        ECC::Hash::Value GetLockImage() const;

    protected:
        BaseTransaction& m_Tx;
        SubTxID m_SubTxID;

        // input
        AmountList m_AmountList;
        Amount m_Fee;
        Amount m_Change;
        Height m_MinHeight;
        Height m_MaxHeight;
        std::vector<Input::Ptr> m_Inputs;
        std::vector<Output::Ptr> m_Outputs;
        ECC::Scalar::Native m_BlindingExcess; // goes to kernel
        ECC::Scalar::Native m_Offset; // goes to offset

        // peer values
        ECC::Scalar::Native m_PartialSignature;
        ECC::Point::Native m_PeerPublicNonce;
        ECC::Point::Native m_PeerPublicExcess;
        std::vector<Input::Ptr> m_PeerInputs;
        std::vector<Output::Ptr> m_PeerOutputs;
        ECC::Scalar::Native m_PeerOffset;
        std::unique_ptr<ECC::Hash::Value> m_PeerLockImage;

        // deduced values, 
        TxKernel::Ptr m_Kernel;
        ECC::Scalar::Native m_PeerSignature;
        ECC::Hash::Value m_Message;
        ECC::Signature::MultiSig m_MultiSig;
    };
}
