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
#include "3rdparty/utilstrencodings.h"
#include "core/proto.h"
#include <algorithm>
#include "wallet/client/extensions/news_channels/version_info.h"
#include "wallet/core/exchange_rate.h"
#include "wallet/core/dex.h"
#include "utility/std_extension.h"

#include <tuple>

namespace beam::wallet
{

#define BEAM_TX_TYPES_MAP(MACRO) \
    MACRO(Simple,               0,  "simple") \
    MACRO(AtomicSwap,           1,  "atomic swap") \
    MACRO(AssetIssue,           2,  "asset issue") \
    MACRO(AssetConsume,         3,  "asset consume") \
    MACRO(AssetReg,             4,  "asset register") \
    MACRO(AssetUnreg,           5,  "asset unregister") \
    MACRO(AssetInfo,            6,  "asset info") \
    MACRO(PushTransaction,      7,  "lelantus mw push") \
    MACRO(PullTransaction,      8,  "lelantus mw pull") \
    MACRO(VoucherRequest,       9,  "lelantus voucher request") \
    MACRO(VoucherResponse,      10, "lelantus voucher response") \
    MACRO(UnlinkFunds,          11, "unlink") \
    MACRO(Contract,             12, "contract") \
    MACRO(DexSimpleSwap,        13, "dex simple swap") \
    MACRO(InstantSbbsMessage,   14, "instant message")

    enum class TxType : uint8_t
    {
#define MACRO(type, index, s) type = index,
        BEAM_TX_TYPES_MAP(MACRO)
#undef MACRO
        ALL
    };

    using TxID = std::array<uint8_t, 16>;
    const Height kDefaultTxLifetime = 2 * 60;
    const Height kDefaultTxResponseTime = 12 * 60;
    extern const char kTimeStampFormat3x3[];
    extern const char kTimeStampFormatCsv[];

    using SubTxID = uint16_t;
    const SubTxID kDefaultSubTxID = 1;

#pragma pack (push, 1)
    struct WalletID
    {
        uintBigFor<BbsChannel>::Type m_Channel;
        PeerID m_Pk;

        WalletID() = default;
        WalletID(Zero_)
            : m_Channel(Zero)
            , m_Pk(Zero)
        {            
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

        BbsChannel get_Channel() const;
        void SetChannelFromPk();

        int cmp(const WalletID&) const;
        COMPARISON_VIA_CMP
    };
#pragma pack (pop)

    boost::optional<PeerID> GetPeerIDFromHex(const std::string& s);

    bool CheckReceiverAddress(const std::string& addr);

    struct PrintableAmount
    {
        explicit PrintableAmount(
            const AmountBig::Type& amount,
            bool /* showPoint */ = false,
            Asset::ID aid = static_cast<Asset::ID>(-1),
            const char* szCoinName = nullptr,
            const char* /* szGrothName */ = nullptr
        )
            :m_value(amount)
            ,m_Aid(aid)
            ,m_szCoinName(szCoinName)
        {
        }

        explicit PrintableAmount(
            const AmountBig::Type& amount,
            bool /* showPoint */,
            Asset::ID aid,
            const std::string& sCoin,
            const std::string& /* sGroth */
        )
            :m_value(amount)
            ,m_Aid(aid)
            ,m_szCoinName(sCoin.c_str())
        {
        }

        const AmountBig::Type& m_value;
        Asset::ID m_Aid;
        const char* m_szCoinName;
    };

    struct Coin;

    enum class TxStatus : uint32_t
    {
        Pending,
        InProgress,
        Canceled,
        Completed,
        Failed,
        Registering,
        Confirming
    };

#define BEAM_TX_FAILURE_REASON_MAP(MACRO) \
    MACRO(Unknown,                       0, "Unexpected reason, please send wallet logs to Beam support") \
    MACRO(Canceled,                      1, "Transaction cancelled") \
    MACRO(InvalidPeerSignature,          2, "Receiver signature in not valid, please send wallet logs to Beam support") \
    MACRO(FailedToRegister,              3, "Failed to register transaction with the blockchain, see node logs for details") \
    MACRO(InvalidTransaction,            4, "Transaction is not valid, please send wallet logs to Beam support") \
    MACRO(InvalidKernelProof,            5, "Invalid kernel proof provided") \
    MACRO(FailedToSendParameters,        6, "Failed to send Transaction parameters") \
    MACRO(NoInputs,                      7, "Not enough inputs to process the transaction") \
    MACRO(ExpiredAddressProvided,        8, "Address is expired") \
    MACRO(FailedToGetParameter,          9, "Failed to get transaction parameters") \
    MACRO(TransactionExpired,            10, "Transaction timed out") \
    MACRO(NoPaymentProof,                11, "Payment not signed by the receiver, please send wallet logs to Beam support") \
    MACRO(MaxHeightIsUnacceptable,       12, "Kernel maximum height is too high") \
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
    MACRO(NoAssetId,                     28, "No valid asset id/asset owner id") \
    MACRO(NoAssetInfo,                   29, "No asset info or asset info is not valid") \
    MACRO(NoAssetMeta,                   30, "No asset metadata or asset metadata is not valid") \
    MACRO(InvalidAssetId,                31, "Invalid asset id") \
    MACRO(AssetConfirmFailed,            32, "Failed to receive asset confirmation") \
    MACRO(AssetInUse,                    33, "Asset is still in use (issued amount > 0)") \
    MACRO(AssetLocked,                   34, "Asset is still locked") \
    /*MACRO(RegisterAmountTooSmall,        35, "Asset registration fee is too small")*/ \
    MACRO(ICAmountTooBig,                36, "Cannot issue/consume more than MAX_INT64 asset groth in one transaction") \
    MACRO(NotEnoughDataForProof,         37, "Some mandatory data for payment proof is missing") \
    MACRO(NoMasterKey,                   38, "Master key is needed for this transaction, but unavailable") \
    MACRO(KeyKeeperError,                39, "Key keeper malfunctioned") \
    MACRO(KeyKeeperUserAbort,            40, "Aborted by the user") \
    MACRO(AssetExists,                   41, "Asset has been already registered") \
    MACRO(InvalidAssetOwnerId,           42, "Invalid asset owner id") \
    MACRO(AssetsDisabledInWallet,        43, "Asset transactions are disabled in the wallet") \
    MACRO(NoVoucher,                     44, "No voucher, no address to receive it") \
    MACRO(AssetsDisabledFork2,           45, "Asset transactions are not available until fork2") \
    MACRO(KeyKeeperNoSlots,              46, "Key keeper out of slots") \
    MACRO(ExtractFeeTooBig,              47, "Cannot extract shielded coin, fee is too big.") \
    MACRO(AssetsDisabledReceiver,        48, "Asset transactions are disabled in the receiver wallet") \
    MACRO(AssetsDisabledInRules,         49, "Asset transactions are disabled in blockchain configuration") \
    MACRO(NoPeerIdentity,                50, "Peer Identity required") \
    MACRO(CannotGetVouchers,             51, "The sender cannot get vouchers for offline transaction") \
    MACRO(Count,                         52, "PLEASE KEEP THIS ALWAYS LAST")

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
            if constexpr (std::is_same<T, WalletID>::value)
            {
                return value.IsValid();
            }
            else
            {
                return true;
            }
        }
        if constexpr (std::is_trivially_destructible<T>::value && std::is_standard_layout<T>::value)
        {
            ZeroObject(value);
        }
        else
        {
            value = T();
        }
        return false;
    }

    bool fromByteBuffer(const ByteBuffer& b, ByteBuffer& value);

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
    ByteBuffer toByteBuffer(const ByteBuffer& value);

    template <typename T>
    std::string to_base64(const T& obj)
    {
        ByteBuffer buffer;
        {
            Serializer s;
            s& obj;
            s.swap_buf(buffer);
        }

        return EncodeBase64(buffer.data(), buffer.size());
    }

    template <typename T>
    T from_base64(const std::string& base64)
    {
        T obj;
        {
            auto data = DecodeBase64(base64.data());

            Deserializer d;
            d.reset(data.data(), data.size());

            d& obj;
        }

        return obj;
    }

    using ShieldedVoucherList = std::vector<ShieldedTxo::Voucher>;

#define BEAM_TX_PUBLIC_PARAMETERS_MAP(MACRO) \
    /*     Name                           Value Type */ \
    MACRO(TransactionType,                 0,   TxType) \
    MACRO(IsSender,                        1,   bool) \
    MACRO(Amount,                          2,   Amount) \
    MACRO(Fee,                             3,   Amount) \
    MACRO(MinHeight,                       4,   Height) \
    MACRO(Message,                         5,   ByteBuffer) \
    MACRO(MyID,                            6,   WalletID) \
    MACRO(PeerID,                          7,   WalletID) \
    MACRO(IsPermanentPeerID,               8,   bool) \
    MACRO(CreateTime,                      10,  Timestamp) \
    MACRO(IsInitiator,                     11,  bool) \
    MACRO(PeerMaxHeight,                   12,  Height) \
    MACRO(AmountList,                      13,  AmountList) \
    MACRO(PreselectedCoins,                14,  CoinIDList) \
    MACRO(Lifetime,                        15,  Height) \
    MACRO(PeerProtoVersion,                16,  uint32_t) \
    MACRO(MaxHeight,                       17,  Height) \
    MACRO(AssetID,                         18,  Asset::ID) \
    MACRO(MyWalletIdentity,                20,  PeerID) \
    MACRO(PeerWalletIdentity,              21,  PeerID) \
    MACRO(PeerResponseTime,                24,  Height) \
    MACRO(SubTxIndex,                      25,  SubTxID) \
    MACRO(PeerPublicSharedBlindingFactor,  26,  ECC::Point) \
    MACRO(IsSelfTx,                        27,  bool) \
    MACRO(AtomicSwapPeerPrivateKey,        29,  uintBig) \
    MACRO(AtomicSwapIsBeamSide,            30,  bool) \
    MACRO(AtomicSwapCoin,                  31,  int32_t) \
    MACRO(AtomicSwapAmount,                32,  Amount) \
    MACRO(AtomicSwapPublicKey,             33,  std::string) \
    MACRO(AtomicSwapPeerPublicKey,         34,  std::string) \
    MACRO(AtomicSwapExternalLockTime,      36,  Height) \
    MACRO(AtomicSwapExternalTx,            37,  std::string) \
    MACRO(AtomicSwapExternalTxID,          38,  std::string) \
    MACRO(AtomicSwapExternalTxOutputIndex, 39,  uint32_t) \
    /* signature parameters */ \
    MACRO(PeerPublicNonce,                 40,  ECC::Point) \
    MACRO(PeerPublicExcess,                50,  ECC::Point) \
    MACRO(PeerSignature,                   60,  ECC::Scalar) \
    MACRO(PeerOffset,                      70,  ECC::Scalar) \
    MACRO(PeerInputs,                      80,  std::vector<Input::Ptr>) \
    MACRO(PeerOutputs,                     81,  std::vector<Output::Ptr>) \
    MACRO(TransactionRegistered,           90,  uint8_t) \
    MACRO(TransactionRegisteredExtraInfo,  91,  std::string) \
    MACRO(FailureReason,                   92,  TxFailureReason) \
    MACRO(PaymentConfirmation,             99,  ECC::Signature) \
    /* MaxPrivacy */ \
    MACRO(MaxPrivacyMinAnonimitySet,       100, uint8_t) \
    /* allows to restore receiver address from */ \
    /*MACRO(PeerSharedBulletProofMSig,       108, ECC::RangeProof::Confidential::Part1) not used */ \
    MACRO(PeerSharedBulletProofPart2,      109, ECC::RangeProof::Confidential::Part2) \
    MACRO(PeerSharedBulletProofPart3,      110, ECC::RangeProof::Confidential::Part3) \
    MACRO(PeerLockImage,                   115, Hash::Value) \
    MACRO(AssetMetadata,                   116, std::string)\
    MACRO(DexOrderID,                      117, DexOrderID) \
    MACRO(ExternalDexOrderID,              118, DexOrderID) \
    MACRO(ExchangeRates,                   120, std::vector<ExchangeRate>) \
    MACRO(OriginalToken,                   121, std::string) \
    /* Lelantus */ \
    MACRO(ShieldedOutputId,                122, TxoID) \
    MACRO(PublicAddreessGen,               123, ShieldedTxo::PublicGen) \
    MACRO(ShieldedVoucherList,             124, ShieldedVoucherList) \
    MACRO(Voucher,                         125, ShieldedTxo::Voucher) \
    MACRO(PublicAddressGenSig,             111, ECC::Signature) \
    /* Version */ \
    MACRO(ClientVersion,                   126, ByteBuffer/*std::string*/) \
    MACRO(LibraryVersion,                  127, ByteBuffer/*std::string*/) \

    // Ids of the transaction parameters
    enum class TxParameterID : uint8_t
    {
        // public parameters
        // Can be set during outside communications
#define MACRO(name, index, type) name = index,
        BEAM_TX_PUBLIC_PARAMETERS_MAP(MACRO)
#undef MACRO

        // private parameters

        ModifyTime = 128,
        KernelProofHeight = 129,

        BlindingExcess = 130,

        KernelUnconfirmedHeight = 133,
        PeerResponseHeight = 134,
        AssetConfirmedHeight = 135, // This is NOT the same as ProofHeight for kernel!
        AssetUnconfirmedHeight = 136,
        AssetInfoFull = 137,
        MinConfirmations = 138, // Minimum confirmations count for simple transactions from settings

        Offset = 140,

        ContractDataPacked = 141,
        HftState = 142,

        UserConfirmationToken = 143,

        Status = 151,
        KernelID = 152,
        MyAddressID = 158, // in case the address used in the tx is eventually deleted, the user should still be able to prove it was owned

        PartialSignature = 159,

        SharedBlindingFactor = 160,
        AggregateSignature = 161,
        NonceSlot = 163,
        PublicNonce = 164,
        PublicExcess = 165,
        MutualTxState = 166,
        SharedCoinID = 172,
        SharedCommitment = 174,

        Inputs = 180,
        ExtraKernels = 181,
        InputCoins = 183,
        OutputCoins = 184,
        InputCoinsShielded = 185,
        Outputs = 190,
        AppID = 191,
        AppName = 192,
        DexReceiveAsset = 193,
        DexReceiveAmount = 194,

        Kernel = 200,
        PreImage = 201,
        AtomicSwapSecretPrivateKey = 202,
        AtomicSwapSecretPublicKey = 203,
        Confirmations = 204,
        AtomicSwapPrivateKey = 205,
        AtomicSwapWithdrawAddress = 206,
        AtomicSwapExternalHeight = 207,
        InternalFailureReason = 210,
        AddressType = 211,
        SavePeerAddress = 212, // allows to preserve and control the old behaviour of saving address 
        TransactionRegisteredInternal = 222, // used to overwrite previouse result
        IsContractNotificationMarkedAsRead = 223,
        State = 255,

        // aliases
        MyAddr = MyID,
        PeerAddr = PeerID,
        MyEndpoint = MyWalletIdentity,
        PeerEndpoint = PeerWalletIdentity,
    };

    using PackedTxParameters = std::vector<std::pair<TxParameterID, ByteBuffer>>;

    // Holds transaction parameters as key/value
    class TxParameters
    {
    public:
        TxParameters(const boost::optional<TxID>& txID = {});

        bool operator==(const TxParameters& other) const;
        bool operator!=(const TxParameters& other) const;

        const boost::optional<TxID>& GetTxID() const;

        template <typename T>
        boost::optional<T> GetParameter(TxParameterID parameterID, SubTxID subTxID = kDefaultSubTxID) const
        {
            auto subTxIt = m_Parameters.find(subTxID);
            if (subTxIt == m_Parameters.end())
            {
                return {};
            }
            auto pit = subTxIt->second.find(parameterID);
            if (pit == subTxIt->second.end())
            {
                return {};
            }
            boost::optional<T> res;
            res.emplace();
            const ByteBuffer& b = pit->second;
            fromByteBuffer(b, *res);
            return res;
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
            fromByteBuffer(b, value);
            return true;
        }

        template <typename T>
        TxParameters& SetParameter(TxParameterID parameterID, const T& value, SubTxID subTxID = kDefaultSubTxID)
        {
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
        TxParameters& SetParameter(TxParameterID parameterID, ByteBuffer&& parameter, SubTxID subTxID = kDefaultSubTxID);
   
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
        bool IsValid() const;
        SERIALIZE(m_Flags, m_TxID, m_Parameters);
    private:
        uint8_t m_Flags = TokenFlag;
        boost::optional<TxID> m_TxID;
        PackedTxParameters m_Parameters;
    };    

    boost::optional<TxParameters> ParseParameters(const std::string& text);

    enum struct TxAddressType : uint8_t
    {
        Unknown,
        Regular,
        AtomicSwap,
        Offline,
        MaxPrivacy,
        PublicOffline
    };

    std::tuple<TxStatus, bool, bool> ParseParamsForStatusInterpretation(const TxParameters& txParams);
    std::string GetTxStatusStr(const TxParameters& txParams);
    std::string GetSimpleTxStatusStr(const TxParameters& txParams);
    std::string GetMaxAnonimityTxStatusStr(const TxParameters& txParams);
    std::string GetAssetTxStatusStr(const TxParameters& txParams);
    std::string GetContractTxStatusStr(const TxParameters& txParams);

    // Specifies key transaction parameters for interaction with Wallet Clients
    struct TxDescription : public TxParameters
    {
        TxDescription() = default;
        TxDescription(const TxID& txId
            , TxType txType           = TxType::Simple
            , Amount amount           = 0
            , Amount fee              = 0
            , Asset::ID assetId       = Asset::s_InvalidID
            , Height minHeight        = 0
            , const WalletID & peerAddr = Zero
            , const WalletID& myAddr  = Zero
            , ByteBuffer&& message    = {}
            , Timestamp createTime    = {}
            , bool sender             = true
        )
            : TxParameters(txId)
            , m_txId{ txId }
            , m_txType{ txType }
            , m_amount{ amount }
            , m_fee{ fee }
            , m_assetId{assetId}
            , m_minHeight{ minHeight }
            , m_peerAddr{ peerAddr }
            , m_myAddr{ myAddr }
            , m_message{ std::move(message) }
            , m_createTime{ createTime }
            , m_modifyTime{ createTime }
            , m_sender{ sender }
        {
        }
        explicit TxDescription(const TxParameters&);
        void fillFromTxParameters(const TxParameters&);

        [[nodiscard]] bool canResume() const;
        [[nodiscard]] bool canCancel() const;
        [[nodiscard]] bool canDelete() const;
        [[nodiscard]] std::string getTxTypeString() const;
        [[nodiscard]] Amount getExchangeRate(const Currency& target, beam::Asset::ID assetId = beam::Asset::s_InvalidID) const;
        [[nodiscard]] std::string getToken() const;
        [[nodiscard]] std::string getSenderEndpoint() const;
        [[nodiscard]] std::string getReceiverEndpoint() const;
        [[nodiscard]] std::string getEndpoint(bool isMy) const;
        [[nodiscard]] std::string getSender() const;
        [[nodiscard]] std::string getReceiver() const;
        [[nodiscard]] std::string getAddressFrom() const;
        [[nodiscard]] std::string getAddressTo() const;

        #define BEAM_TX_DESCRIPTION_INITIAL_PARAMS(macro) \
        macro(TxParameterID::TransactionType,   TxType,          m_txType,          wallet::TxType::Simple) \
        macro(TxParameterID::Amount,            Amount,          m_amount,          0) \
        macro(TxParameterID::Fee,               Amount,          m_fee,             0) \
        macro(TxParameterID::AssetID,           Asset::ID,       m_assetId,         Asset::s_InvalidID) \
        macro(TxParameterID::AssetMetadata,     std::string,     m_assetMeta,       {}) \
        macro(TxParameterID::MinHeight,         Height,          m_minHeight,       0) \
        macro(TxParameterID::PeerAddr,          WalletID,        m_peerAddr,        Zero) \
        macro(TxParameterID::MyAddr,            WalletID,        m_myAddr,          Zero) \
        macro(TxParameterID::Message,           ByteBuffer,      m_message,         {}) \
        macro(TxParameterID::CreateTime,        Timestamp,       m_createTime,      0) \
        macro(TxParameterID::ModifyTime,        Timestamp,       m_modifyTime,      0) \
        macro(TxParameterID::IsSender,          bool,            m_sender,          false) \
        macro(TxParameterID::IsSelfTx,          bool,            m_selfTx,          false) \
        macro(TxParameterID::Status,            TxStatus,        m_status,          TxStatus::Pending) \
        macro(TxParameterID::KernelID,          Merkle::Hash,    m_kernelID,        Zero) \
        macro(TxParameterID::FailureReason,     TxFailureReason, m_failureReason,   TxFailureReason::Unknown) \
        macro(TxParameterID::AppID,             std::string,     m_appID,           std::string()) \
        macro(TxParameterID::AppName,           std::string,     m_appName,         std::string())

        TxID m_txId = {};
        #define MACRO(id, type, field, init) type field = init;
        BEAM_TX_DESCRIPTION_INITIAL_PARAMS(MACRO)
        #undef MACRO
    };

    std::string interpretStatus(const TxDescription& tx);

    // messages
    struct SetTxParameter
    {
        WalletID m_From = Zero;
        TxID m_TxID = {};

        TxType m_Type = TxType::Simple;

        PackedTxParameters m_Parameters = {};
        
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
            fromByteBuffer(b, value);
            return true;
        }

        template <typename T>
        T GetParameterOrDefault(TxParameterID paramID, const T& defval = T()) const
        {
            T val;
            if (GetParameter(paramID, val))
            {
                return val;
            }
            return defval;
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

    struct IRawCommGateway
    {
        struct IHandler {
            virtual void OnMsg(const Blob&) = 0;
        };

        virtual void Listen(const WalletID&, const ECC::Scalar::Native& sk, IHandler* = nullptr) {}
        virtual void Unlisten(const WalletID&, IHandler* = nullptr) {}
        virtual void Send(const WalletID& peerAddr, const Blob&) {}
    };

    struct INegotiatorGateway
        :public IAsyncContext
        ,public IRawCommGateway
    {
        using ShieldedListCallback = std::function<void(TxoID, uint32_t, proto::ShieldedList&)>;
        using ProofShildedOutputCallback = std::function<void(proto::ProofShieldedOutp&)>;
        virtual ~INegotiatorGateway() {}
        virtual void on_tx_completed(const TxID& ) = 0;
        virtual void on_tx_failed(const TxID&) = 0;
        virtual void register_tx(const TxID&, const Transaction::Ptr&, const Merkle::Hash* pParentCtx = nullptr, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual void confirm_kernel(const TxID&, const Merkle::Hash& kernelID, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual void confirm_asset(const TxID& txID, const PeerID& ownerID, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual void confirm_asset(const TxID& txID, const Asset::ID assetId, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual void get_kernel(const TxID&, const Merkle::Hash& kernelID, SubTxID subTxID = kDefaultSubTxID) = 0;
        virtual bool get_tip(Block::SystemState::Full& state) const = 0;
        virtual void send_tx_params(const WalletID& peerAddr, const SetTxParameter&) = 0;
        virtual void get_shielded_list(const TxID&, TxoID startIndex, uint32_t count, ShieldedListCallback&& callback) = 0;
        virtual void get_proof_shielded_output(const TxID&, const ECC::Point& serialPublic, ProofShildedOutputCallback&& callback) {};
        virtual void UpdateOnNextTip(const TxID&) = 0;
        virtual void get_UniqueVoucher(const WalletID& peerAddr, const TxID& txID, boost::optional<ShieldedTxo::Voucher>&) {}

        struct IConfirmCallback
        {
            typedef std::unique_ptr<IConfirmCallback> Ptr;
            virtual ~IConfirmCallback() {}
            virtual void OnDone(const Height*) = 0;
        };
        virtual void confirm_kernel_ex(const Merkle::Hash& kernelID, IConfirmCallback::Ptr&&) = 0;

        virtual void HftSubscribe(bool) {}
        virtual const Merkle::Hash* get_DependentState(uint32_t& nCount) { nCount = 0; return nullptr; }
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
        Amount m_Value = 0;
        Asset::ID m_AssetID = Asset::s_InvalidID;
        ECC::Hash::Value m_KernelID;
        PeerID m_Sender;

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
 
    bool LoadReceiverParams(const TxParameters& receiverParams, TxParameters& params, TxAddressType addressType);

    // Check current time with the timestamp of last received block
    // If it is more than 10 minutes, the walelt is considered not in sync
    bool IsValidTimeStamp(Timestamp currentBlockTime_s, Timestamp tolerance_s = 60 * 10); // 10 minutes tolerance.

    std::string GetSendToken(const std::string& sbbsAddress, const std::string& endpointStr, Amount amount);

    struct IPrivateKeyKeeper2;
    ShieldedVoucherList GenerateVoucherList(const std::shared_ptr<IPrivateKeyKeeper2>&, uint64_t ownID, size_t count);
    bool IsValidVoucherList(const ShieldedVoucherList& vouchers, const PeerID& identity);

    std::string ConvertTokenToJson(const std::string& token);
    std::string ConvertJsonToToken(const std::string& json);

    // add timestamp to the file name
    std::string TimestampFile(const std::string& fileName);

    extern bool g_AssetsEnabled; // global flag
    TxFailureReason CheckAssetsEnabled(Height h);
    bool isFork3(Height h);

    void AppendLibraryVersion(TxParameters& params);

    using VersionFunc = std::function<void(const std::string&, const std::string&)>;
    void ProcessLibraryVersion(const TxParameters& params, VersionFunc&& func = {});
    void ProcessClientVersion(const TxParameters& params, const std::string& appName, const std::string& myClientVersion, const std::string& libVersion, VersionFunc&& func);
    uint32_t GetShieldedInputsNum(const std::vector<TxKernel::Ptr>&);
    TxAddressType GetAddressType(const TxDescription& tx);
    TxAddressType GetAddressType(const std::string& address);

    const char* get_BroadcastValidatorPublicKey();

}    // beam::wallet

namespace beam
{
    template <typename E>
    using UnderlyingType = typename std::underlying_type<E>::type;

    template <typename E>
    using EnumTypesOnly = typename std::enable_if<std::is_enum<E>::value, E>::type;

    template <typename E, typename = EnumTypesOnly<E>>
    constexpr UnderlyingType<E> underlying_cast(E e)
    {
        return static_cast<UnderlyingType<E>>(e);
    }

    std::ostream& operator<<(std::ostream& os, const wallet::PrintableAmount& amount);
    std::ostream& operator<<(std::ostream& os, const wallet::TxID& uuid);
}  // namespace beam

namespace std
{
    string to_string(const beam::wallet::WalletID&);
    string to_string(const beam::Merkle::Hash& hash);
    string to_string(const beam::wallet::PrintableAmount& amount);
    string to_string(const beam::wallet::TxParameters&);
    string to_string(const beam::wallet::TxID&);
    string to_string(const beam::PeerID&);
    string to_base58(const beam::PeerID&);
    string to_string(const beam::AmountBig::Type&);

    template<>
    struct hash<beam::wallet::WalletID>
    {
        size_t operator() (const beam::wallet::WalletID& key) const noexcept
        {
            return std::hash<ECC::uintBig>{}(key.m_Pk);
        }
    };

    unsigned to_unsigned(const std::string&, bool throws = true);
}
