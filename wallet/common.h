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

#pragma pack (push, 1)
	struct WalletID
	{
		uintBigFor<BbsChannel>::Type m_Channel;
		PeerID m_Pk;

		WalletID() {}
		WalletID(Zero_)
		{
			m_Channel = Zero;
			m_Pk = Zero;
		}

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Channel
				& m_Pk;
		}

		bool FromBuf(const ByteBuffer&);
		bool FromHex(const std::string&);

		bool IsValid() const; // isn't cheap

		int cmp(const WalletID&) const;
		COMPARISON_VIA_CMP
	};
#pragma pack (pop)

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
        WalletID m_peerId = Zero;
        WalletID m_myId = Zero;
        ByteBuffer m_message;
        Timestamp m_createTime=0;
        Timestamp m_modifyTime=0;
        bool m_sender=false;
        TxStatus m_status=TxStatus::Pending;
        ByteBuffer m_fsmState;

        bool canResume() const
        {
            return m_status == TxStatus::Pending 
                || m_status == TxStatus::InProgress 
                || m_status == TxStatus::Registered;
        }

        bool canCancel() const
        {
            return m_status == beam::TxStatus::InProgress
                || m_status == beam::TxStatus::Pending;
        }

        bool canDelete() const
        {
            return m_status == beam::TxStatus::Failed
                || m_status == beam::TxStatus::Completed
                || m_status == beam::TxStatus::Cancelled;
        }
    };

    namespace wallet
    {
        template <typename T>
        ByteBuffer toByteBuffer(const T& value)
        {
            Serializer s;
            s & value;
            ByteBuffer b;
            s.swap_buf(b);
            return b;
        }

        ByteBuffer toByteBuffer(const ECC::Point::Native& value);
        ByteBuffer toByteBuffer(const ECC::Scalar::Native& value);

        enum class TxParameterID : uint8_t
        {
            // public parameters
            TransactionType = 0,
            IsSender = 1,
            Amount = 2,
            Fee = 3,
            MinHeight = 4,
            Message = 5,
            MyID = 6,
            PeerID = 7,
            //Inputs = 8,
            //Outputs = 9,
            CreateTime = 10,
            IsInitiator = 11,
            MaxHeight = 12,

            AtomicSwapCoin = 20,
            AtomicSwapAmount = 21,

            LockedAmount = 25,
            LockedMinHeight = 26,
            

            PeerPublicSharedBlindingFactor = 23,

            // signature parameters

            PeerPublicNonce = 40,
            SharedPeerPublicNonce = 41,
            LockedPeerPublicNonce = 42,

            PeerPublicExcess = 50,
            SharedPeerPublicExcess = 51,
            LockedPeerPublicExcess = 52,

            PeerSignature = 60,
            SharedPeerSignature = 61,
            LockedPeerSignature = 62,

            PeerOffset = 70,
            SharedPeerOffset = 71,
            LockedPeerOffset = 72,

            PeerInputs = 80,
            LockedPeerInputs = 82,
            PeerOutputs = 81,
            LockedPeerOutputs = 83,
            SharedPeerInputs = 84,
            SharedPeerOutputs = 85,

            TransactionRegistered = 90,

            FailureReason = 92,

            // private parameters
            PrivateFirstParam = 128,

            ModifyTime = 128,
            KernelProofHeight = 129,

            BlindingExcess = 130,
            SharedBlindingExcess = 131,
            LockedBlindingExcess = 132,

            Offset = 140,
            SharedOffset = 141,
            LockedOffset = 142,

            Change = 150,
            Status = 151,

            SharedBlindingFactor = 160,
            LockedBlindingFactor = 161,
			MyNonce = 162,
            SharedPeerBlindingFactor = 170,

            Inputs = 180,
            SharedInputs = 181,
            LockedInputs = 182,
            
            Outputs = 190,
            SharedOutputs = 191,
            LockedOutputs = 192,
            State = 255

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
            WalletID m_From;
            TxID m_TxID;

            TxType m_Type;

            std::vector<std::pair<TxParameterID, ByteBuffer>> m_Parameters;

            template <typename T>
            SetTxParameter& AddParameter(TxParameterID paramID, T&& value)
            {
                m_Parameters.emplace_back(paramID, toByteBuffer(value));
                return *this;
            }

            SERIALIZE(m_From, m_TxID, m_Type, m_Parameters);
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
            virtual void send_tx_params(const WalletID& peerID, SetTxParameter&&) = 0;
        };
    }
}

namespace std
{
    string to_string(const beam::WalletID&);
}
