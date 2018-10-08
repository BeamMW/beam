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

#include "core/common.h"
#include "core/ecc_native.h"

#include "core/serialization_adapters.h"
#include "core/proto.h"

namespace beam
{
    using TxID = std::array<uint8_t, 16>;
    using WalletID = PeerID;

    struct PrintableAmount
    {
        explicit PrintableAmount(const Amount& amount, bool showPoint = false) : m_value{ amount }, m_showPoint{showPoint}
        {}
        const Amount& m_value;
        bool m_showPoint;
    };

    std::ostream& operator<<(std::ostream& os, const PrintableAmount& amount);
    std::ostream& operator<<(std::ostream& os, const TxID& uuid);

    struct Coin;

    enum class TxStatus : uint32_t
    {
        Pending,
        InProgress,
        Cancelled,
        Completed,
        Failed,
        Registered
    };

    struct TxDescription
    {
        TxDescription() = default;

        TxDescription(const TxID& txId
                    , Amount amount
                    , Amount fee
                    , Height minHeight
                    , const WalletID& peerId
                    , const WalletID& myId
                    , ByteBuffer&& message
                    , Timestamp createTime
                    , bool sender)
            : m_txId{ txId }
            , m_amount{ amount }
            , m_fee{ fee }
			, m_change{}
            , m_minHeight{ minHeight }
            , m_peerId{ peerId }
            , m_myId{myId}
            , m_message{ std::move(message) }
            , m_createTime{ createTime }
            , m_modifyTime{ createTime }
            , m_sender{ sender }
            , m_status{ TxStatus::Pending }
            , m_fsmState{}
        {}

        TxID m_txId;
        Amount m_amount=0;
        Amount m_fee=0;
		Amount m_change=0;
        Height m_minHeight=0;
        WalletID m_peerId = {0};
        WalletID m_myId = {0};
        ByteBuffer m_message;
        Timestamp m_createTime=0;
        Timestamp m_modifyTime=0;
        bool m_sender=false;
        TxStatus m_status=TxStatus::Failed;
        ByteBuffer m_fsmState;

        bool canResume() const
        {
            return m_status == TxStatus::Pending 
                || m_status == TxStatus::InProgress 
                || m_status == TxStatus::Registered;
        }
    };

    namespace wallet
    {
        std::pair<ECC::Scalar::Native, ECC::Scalar::Native> splitKey(const ECC::Scalar::Native& key, uint64_t index);

        enum class TxParameterID : uint32_t
        {
            // public parameters
            TransactionType = 0,
            IsSender,
            Amount,
            Fee,
            MinHeight,
            Message,
            MyID,
            PeerID,
            Inputs,
            Outputs,
            CreateTime,

            AtomicSwapCoin,
            AtomicSwapAmount,

            PeerPublicNonce,
            PeerPublicExcess,
            PeerSignature,
            PeerOffset,
            PeerInputs,
            PeerOutputs,

            TransactionRegistered,
            KernelProof,
            FailureReason,

            // private parameters
            PrivateFirstParam = 1 << 16,

            ModifyTime,
            BlindingExcess,
            Offset,
            Change,
            Status
        };

        enum class TxType : uint8_t
        {
            Simple,
            AtomicSwap
        };

        enum class AtomicSwapCoin
        {
            Bitcoin
        };

        // messages
        struct SetTxParameter
        {
            WalletID m_from;
            TxID m_txId;

            TxType m_Type;

            std::vector<std::pair<TxParameterID, ByteBuffer>> m_Parameters;

            SERIALIZE(m_from, m_txId, m_Type, m_Parameters);
            static const size_t MaxParams = 20;
        };

        struct INegotiatorGateway
        {
            virtual ~INegotiatorGateway() {}
            virtual void on_tx_completed(const TxID& ) = 0;
            virtual void register_tx(const TxID&, Transaction::Ptr) = 0;
            virtual void confirm_outputs(const std::vector<Coin>&) = 0;
            virtual void confirm_kernel(const TxID&, const TxKernel&) = 0;
            virtual bool get_tip(Block::SystemState::Full& state) const = 0;
            virtual bool isTestMode() const = 0;
            virtual void send_tx_params(const WalletID& peerID, SetTxParameter&&) = 0;
        };
    }
}

namespace std
{
    string to_string(const beam::WalletID&);
}
