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

#include "core/ecc_native.h"
#include "core/merkle.h"

#include "core/serialization_adapters.h"
#include "core/proto.h"
#include <algorithm>

namespace beam::wallet
{
    enum class TxType : uint8_t
    {
        Simple,
        AtomicSwap,
        AssetIssue,
        AssetConsume,
        AssetReg,
        AssetUnreg,
        AssetInfo,
        ALL
    };

    using TxID = std::array<uint8_t, 16>;
    const Height kDefaultTxLifetime = 2 * 60;
    const Height kDefaultTxResponseTime = 12 * 60;
    const char kTimeStampFormat3x3[] = "%Y.%m.%d %H:%M:%S";
    const char kTimeStampFormatCsv[] = "%d %b %Y | %H:%M";

    using SubTxID = uint16_t;
    const SubTxID kDefaultSubTxID = 1;
    constexpr Amount kMinFeeInGroth = 100;

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

    bool check_receiver_address(const std::string& addr);

    struct PrintableAmount
    {
        explicit PrintableAmount(const Amount& amount, bool showPoint = false, const std::string& coinName = "", const std::string& grothName ="")
            : m_value{amount}
            , m_showPoint{showPoint}
            , m_coinName{coinName}
            , m_grothName{grothName}
        {}

        const Amount& m_value;
        bool m_showPoint;
        std::string m_coinName;
        std::string m_grothName;
    };
    
    struct Coin;

    enum class TxStatus : uint32_t
    {
        Pending,
        InProgress,
        Canceled,
        Completed,
        Failed,
        Registering
    };

#define BEAM_TX_FAILURE_REASON_MAP(MACRO) \
    MACRO(Unknown,                       0, "Unknown reason") \
    MACRO(Canceled,                      1, "Transaction was cancelled") \
    MACRO(InvalidPeerSignature,          2, "Peer's signature is not valid ") \
    MACRO(FailedToRegister,              3, "Failed to register transaction") \
    MACRO(InvalidTransaction,            4, "Transaction is not valid") \
    MACRO(InvalidKernelProof,            5, "Invalid kernel proof provided") \
    MACRO(FailedToSendParameters,        6, "Failed to send tx parameters") \
    MACRO(NoInputs,                      7, "Not enough inputs to process the transaction") \
    MACRO(ExpiredAddressProvided,        8, "Address is expired") \
    MACRO(FailedToGetParameter,          9, "Failed to get parameter") \
    MACRO(TransactionExpired,            10, "Transaction has expired") \
    MACRO(NoPaymentProof,                11, "Payment not signed by the receiver") \
    MACRO(MaxHeightIsUnacceptable,       12, "Kernel's max height is unacceptable") \
    MACRO(InvalidState,                  13, "Transaction has invalid state") \
    MACRO(SubTxFailed,                   14, "Subtransaction has failed") \
    MACRO(SwapInvalidAmount,             15, "Contract's amount is not valid") \
    MACRO(SwapInvalidContract,           16, "Side chain has invalid contract") \
    MACRO(SwapSecondSideBridgeError,     17, "Side chain bridge has internal error") \
    MACRO(SwapNetworkBridgeError,        18, "Side chain bridge has network error") \
    MACRO(SwapFormatResponseError,       19, "Side chain bridge has response format error") \
    MACRO(InvalidCredentialsOfSideChain, 20, "Invalid credentials of Side chain") \
    MACRO(NotEnoughTimeToFinishBtcTx,    21, "Not enough time to finish btc lock transaction") \
    MACRO(FailedToCreateMultiSig,        22, "Failed to create multi-signature") \
    MACRO(FeeIsTooSmall,                 23, "Fee is too small") \
    MACRO(FeeIsTooLarge,                 24, "Fee is too large") \
    MACRO(MinHeightIsUnacceptable,       25, "Kernel's min height is unacceptable") \
    MACRO(NotLoopback,                   26, "Not a loopback transaction") \
    MACRO(NoKeyKeeper,                   27, "Key keeper is not initialized") \
    MACRO(NoAssetId,                     28, "No valid asset owner id/asset owner idx") \
    MACRO(NoAssetInfo,                   29, "No asset info or asset info is not valid") \
    MACRO(NoAssetMeta,                   30, "No asset metadata or asset metadata is not valid") \
    MACRO(InvalidAssetId,                31, "Invalid asset id") \
    MACRO(AssetConfirmFailed,            32, "Failed to receive asset confirmation") \
    MACRO(AssetInUse,                    33, "Asset is still in use (issued amount > 0)") \
    MACRO(AssetLocked,                   34, "Asset is still locked") \
    MACRO(RegisterAmountTooSmall,        35, "Asset registration fee is too small") \
    MACRO(ICAmountTooBig,                36, "Cannot issue/consume more than MAX_INT64 asset groth in one transaction") \
    MACRO(NotEnoughDataForProof,         37, "Some mandatory data for payment proof is missing") \
    MACRO(NoMasterKey,                   38, "Master key is needed for this transaction, but unavailable") \
    MACRO(KeyKeeperError,                39, "Key keeper malfunctioned") \
    MACRO(KeyKeeperUserAbort,            40, "Aborted by the user")

    enum TxFailureReason : int32_t
    {
#define MACRO(name, code, _) name = code, 
        BEAM_TX_FAILURE_REASON_MAP(MACRO)
#undef MACRO
    };

    template<typename T>
    bool fromByteBuffer(const ByteBuffer& b, T& value)
    {
        if (!b.empty())
        {
            Deserializer d;
            d.reset(b.data(), b.size());
            d & value;
            return true;
        }
        ZeroObject(value);
        return false;
    }

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

    Amount GetMinimumFee(size_t numberOfOutputs, size_t numberOfKenrnels = 1);

    // Ids of the transaction parameters
    enum class TxParameterID : uint8_t
    {
        // public parameters
        // Can be set during outside communications
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
        PeerMaxHeight = 12,
        AmountList = 13,
        PreselectedCoins = 14,
        Lifetime = 15,
        PeerProtoVersion = 16,
        MaxHeight = 17,
        AssetID = 18,

        MySecureWalletID = 20,
        PeerSecureWalletID = 21,

        PeerResponseTime = 24,
        SubTxIndex = 25,
        PeerPublicSharedBlindingFactor = 26,

        IsSelfTx = 27,

        AtomicSwapPeerPrivateKey = 29,
        AtomicSwapIsBeamSide = 30,
        AtomicSwapCoin = 31,
        AtomicSwapAmount = 32,
        AtomicSwapPublicKey = 33,
        AtomicSwapPeerPublicKey = 34,
        AtomicSwapLockTime = 35,
        AtomicSwapExternalLockTime = 36,
        AtomicSwapExternalTx = 37,
        AtomicSwapExternalTxID = 38,
        AtomicSwapExternalTxOutputIndex = 39,

        // signature parameters

        PeerPublicNonce = 40,

        PeerPublicExcess = 50,

        PeerSignature = 60,

        PeerOffset = 70,

        PeerInputs = 80,
        PeerOutputs = 81,

        TransactionRegistered = 90,

        FailureReason = 92,

        PaymentConfirmation = 99,

        PeerSharedBulletProofMSig = 108,
        PeerSharedBulletProofPart2 = 109,
        PeerSharedBulletProofPart3 = 110,

        PeerLockImage = 115,
        AssetOwnerIdx = 116,
        AssetMetadata = 117,

        // private parameters
        PrivateFirstParam = 128,

        ModifyTime = 128,
        KernelProofHeight = 129,

        BlindingExcess = 130,

        KernelUnconfirmedHeight = 133,
        PeerResponseHeight = 134,
        AssetConfirmedHeight = 135, // This is NOT the same as ProofHeight for kernel!
        AssetUnconfirmedHeight = 136,
        AssetFullInfo = 137,

        Offset = 140,

        UserConfirmationToken = 143,

        ChangeAsset = 149,
        ChangeBeam = 150,
        Status = 151,
        KernelID = 152,
        MyAddressID = 158, // in case the address used in the tx is eventually deleted, the user should still be able to prove it was owned

        PartialSignature = 159,

        SharedBlindingFactor = 160,
        MyNonce = 162,
        NonceSlot = 163,
        PublicNonce = 164,
        PublicExcess = 165,
        SharedBulletProof = 171,
        SharedCoinID = 172,
        SharedSeed = 173,

        Inputs = 180,
        InputCoins = 183,
        OutputCoins = 184,
        Outputs = 190,

        Kernel = 200,
        PreImage = 201,
        AtomicSwapSecretPrivateKey = 202,
        AtomicSwapSecretPublicKey = 203,
        Confirmations = 204,
        AtomicSwapPrivateKey = 205,
        AtomicSwapWithdrawAddress = 206,
        AtomicSwapExternalHeight = 207,

        InternalFailureReason = 210,
    
        State = 255

    };

    using PackedTxParameters = std::vector<std::pair<TxParameterID, ByteBuffer>>;

    // Holds transaction parameters as key/value
    class TxParameters
    {
    public:
        TxParameters(const boost::optional<TxID>& txID = {});

        bool operator==(const TxParameters& other);
        bool operator!=(const TxParameters& other);

        const boost::optional<TxID>& GetTxID() const;

        template <typename T>
        boost::optional<T> GetParameter(TxParameterID parameterID, SubTxID subTxID = kDefaultSubTxID) const
        {
            static_assert(std::is_same<T, ByteBuffer>::value == false);
            auto buffer = GetParameter(parameterID, subTxID);
            if (buffer && !buffer->empty())
            {
                Deserializer d;
                d.reset(buffer->data(), buffer->size());
                T value;
                d & value;
                return value;
            }
            return boost::optional<T>();
        }

        template <typename T>
        bool GetParameter(TxParameterID parameterID, T& value, SubTxID subTxID = kDefaultSubTxID) const
        {
            auto subTxIt = m_Parameters.find(subTxID);
            if (subTxIt == m_Parameters.end())
            {
                return false;
            }
            auto pit = subTxIt->second.find(parameterID);
            if (pit == subTxIt->second.end())
            {
                return false;
            }
            const ByteBuffer& b = pit->second;

            if (!b.empty())
            {
                Deserializer d;
                d.reset(b.data(), b.size());
                d& value;
            }
            else
            {
                ZeroObject(value);
            }
            return true;
        }

        template <typename T>
        TxParameters& SetParameter(TxParameterID parameterID, const T& value, SubTxID subTxID = kDefaultSubTxID)
        {
            static_assert(std::is_same<T, ByteBuffer>::value == false);
            return SetParameter(parameterID, toByteBuffer(value), subTxID);
        }

        bool DeleteParameter(TxParameterID parameterID, SubTxID subTxID = kDefaultSubTxID)
        {
            auto subTxIt = m_Parameters.find(subTxID);
            if (subTxIt == m_Parameters.end())
            {
                return false;
            }
            auto pit = subTxIt->second.find(parameterID);
            if (pit == subTxIt->second.end())
            {
                return false;
            }
            
            subTxIt->second.erase(pit);

            return true;
        }

        PackedTxParameters Pack() const;

        boost::optional<ByteBuffer> GetParameter(TxParameterID parameterID, SubTxID subTxID = kDefaultSubTxID) const;
        TxParameters& SetParameter(TxParameterID parameterID, const ByteBuffer& parameter, SubTxID subTxID = kDefaultSubTxID);

    private:
        boost::optional<TxID> m_ID;
        std::map<SubTxID, std::map<TxParameterID, ByteBuffer>> m_Parameters;
    };

    // Class to simplify serializing/deserializing parameters
    class TxToken
    {
    public:
        static const uint8_t TokenFlag = 0x80;
        TxToken() = default;
        TxToken(const TxParameters&);
        TxParameters UnpackParameters() const;
        SERIALIZE(m_Flags, m_TxID, m_Parameters);
    private:
        uint8_t m_Flags = TokenFlag;
        boost::optional<TxID> m_TxID;
        PackedTxParameters m_Parameters;
    };    

    boost::optional<TxParameters> ParseParameters(const std::string& text);

    // Specifies key transaction parameters for interaction with Wallet Clients
    struct TxDescription : public TxParameters
    {
        TxDescription() = default;

        TxDescription(const TxID& txId
            , TxType txType = TxType::Simple
            , Amount amount = 0
            , Amount fee =0
            , Asset::ID assetId = Asset::s_InvalidID
            , Height minHeight = 0
            , const WalletID & peerId = Zero
            , const WalletID& myId = Zero
            , ByteBuffer&& message = {}
            , Timestamp createTime = {}
            , bool sender = true)
            : TxParameters(txId)
            , m_txId{ txId }
            , m_txType{ txType }
            , m_amount{ amount }
            , m_fee{ fee }
            , m_changeBeam{0}
            , m_changeAsset{0}
            , m_assetId{assetId}
            , m_minHeight{ minHeight }
            , m_peerId{ peerId }
            , m_myId{ myId }
            , m_message{ std::move(message) }
            , m_createTime{ createTime }
            , m_modifyTime{ createTime }
            , m_sender{ sender }
            , m_selfTx{ false }
            , m_status{ TxStatus::Pending }
            , m_kernelID{ Zero }
            , m_failureReason{ TxFailureReason::Unknown }
        {

        }

        bool canResume() const;
        bool canCancel() const;
        bool canDelete() const;
        std::string getStatusString() const;
        std::string getStatusStringApi() const;

    //private:
        TxID m_txId = {};
        wallet::TxType m_txType = wallet::TxType::Simple;
        Amount m_amount = 0;
        Amount m_fee = 0;
        Amount m_changeBeam = 0;
        Amount m_changeAsset = 0;
        Asset::ID m_assetId = Asset::s_InvalidID;
        Key::Index m_assetOwnerIdx = 0;
        Height m_minHeight = 0;
        WalletID m_peerId = Zero;
        WalletID m_myId = Zero;
        ByteBuffer m_message;
        Timestamp m_createTime = 0;
        Timestamp m_modifyTime = 0;
        bool m_sender = false;
        bool m_selfTx = false;
        TxStatus m_status = TxStatus::Pending;
        Merkle::Hash m_kernelID = Zero;
        TxFailureReason m_failureReason = TxFailureReason::Unknown;
    };

    // messages
    struct SetTxParameter
    {
        WalletID m_From;
        TxID m_TxID;

        TxType m_Type;

        PackedTxParameters m_Parameters;
        
        // TODO use TxParameters here
        template <typename T>
        SetTxParameter& AddParameter(TxParameterID paramID, T&& value)
        {
            m_Parameters.emplace_back(paramID, toByteBuffer(value));
            return *this;
        }

        template <typename T>
        bool GetParameter(TxParameterID paramID, T& value) const 
        {
            auto pit = std::find_if(m_Parameters.begin(), m_Parameters.end(), [paramID](const auto& p) { return p.first == paramID; });
            if (pit == m_Parameters.end())
            {
                return false;
            }
            const ByteBuffer& b = pit->second;
                
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

        SERIALIZE(m_From, m_TxID, m_Type, m_Parameters);
    };

    // context to take into account all async wallet operations
    struct IAsyncContext
    {
        virtual void OnAsyncStarted() = 0;
        virtual void OnAsyncFinished() = 0;
    };

    class AsyncContextHolder
    {
    public:
        AsyncContextHolder(IAsyncContext& context)
            : m_Context(context)
        {
            m_Context.OnAsyncStarted();
        }
        ~AsyncContextHolder()
        {
            m_Context.OnAsyncFinished();
        }
    private:
        IAsyncContext& m_Context;
    };

    struct INegotiatorGateway : IAsyncContext
    {
        virtual ~INegotiatorGateway() {}
        virtual void on_tx_completed(const TxID& ) = 0;
        virtual void register_tx(const TxID&, Transaction::Ptr, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual void confirm_outputs(const std::vector<Coin>&) = 0;
        virtual void confirm_kernel(const TxID&, const Merkle::Hash& kernelID, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual void confirm_asset(const TxID& txID, const Key::Index ownerIdx, const PeerID& ownerID, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual void confirm_asset(const TxID& txID, const Asset::ID assetId, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual void get_kernel(const TxID&, const Merkle::Hash& kernelID, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual bool get_tip(Block::SystemState::Full& state) const = 0;
        virtual void send_tx_params(const WalletID& peerID, const SetTxParameter&) = 0;
        virtual void UpdateOnNextTip(const TxID&) = 0;
    };

    enum class ErrorType : uint8_t
    {
        NodeProtocolBase,
        NodeProtocolIncompatible,
        ConnectionBase,
        ConnectionTimedOut,
        ConnectionRefused,
        ConnectionHostUnreach,
        ConnectionAddrInUse,
        TimeOutOfSync,
        InternalNodeStartFailed,
        HostResolvedError,
        ImportRecoveryError,
    };

    ErrorType getWalletError(proto::NodeProcessingException::Type exceptionType);
    ErrorType getWalletError(io::ErrorCode errorCode);

    struct ConfirmationBase
    {
        ECC::Signature m_Signature;
        
        virtual void get_Hash(ECC::Hash::Value&) const = 0;

        bool IsValid(const PeerID&) const;
        void Sign(const ECC::Scalar::Native& sk);
    };

    struct PaymentConfirmation : public ConfirmationBase
    {
        // I, the undersigned, being healthy in mind and body, hereby accept they payment specified below, that shall be delivered by the following kernel ID.
        Amount m_Value;
        Asset::ID m_AssetID = Asset::s_InvalidID;
        ECC::Hash::Value m_KernelID;
        PeerID m_Sender;

        void get_Hash(ECC::Hash::Value&) const override;
    };
    
    struct SwapOfferConfirmation : public ConfirmationBase
    {
        // Identifies owner for swap offer modification
        ByteBuffer m_offerData;

        void get_Hash(ECC::Hash::Value&) const override;
    };

    struct SignatureHandler : public ConfirmationBase
    {
        ByteBuffer m_data;
        void get_Hash(ECC::Hash::Value&) const override;
    };

    uint64_t get_RandomID();

    template<typename Observer, typename Notifier>
    struct ScopedSubscriber
    {
        ScopedSubscriber(Observer* observer, const std::shared_ptr<Notifier>& notifier)
            : m_observer(observer)
            , m_notifier(notifier)
        {
            m_notifier->Subscribe(m_observer);
        }

        ~ScopedSubscriber()
        {
            m_notifier->Unsubscribe(m_observer);
        }
    private:
        Observer * m_observer;
        std::shared_ptr<Notifier> m_notifier;
    };
}  // beam::wallet

namespace beam
{
    std::ostream& operator<<(std::ostream& os, const wallet::PrintableAmount& amount);
    std::ostream& operator<<(std::ostream& os, const wallet::TxID& uuid);

    struct Version
    {
        uint32_t m_major;
        uint32_t m_minor;
        uint32_t m_revision;

        Version() = default;
        Version(uint32_t major, uint32_t minor, uint32_t rev)
            : m_major(major)
            , m_minor(minor)
            , m_revision(rev)
        {};

        SERIALIZE(m_major, m_minor, m_revision);

        // static Version getCurrent();

        std::string to_string() const;
        bool operator==(const Version& other) const;
        bool operator!=(const Version& other) const;
        bool operator<(const Version& other) const;
    };
}  // namespace beam

namespace std
{
    string to_string(const beam::wallet::WalletID&);
    string to_string(const beam::Merkle::Hash& hash);
    string to_string(const beam::wallet::PrintableAmount& amount);
    string to_string(const beam::wallet::TxParameters&);
    string to_string(const beam::Version&);
}
