// Copyright 2019 The Beam Team;
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


#include "common.h"
#include "utility/logger.h"
#include "core/ecc_native.h"
#include "core/base58.h"
#include "core/serialization_adapters.h"
#include "base58.h"
#include "utility/string_helpers.h"
#include "strings_resources.h"
#include "core/shielded.h"
#include "3rdparty/nlohmann/json.hpp"
#include "wallet_db.h"
#include "version.h"

#include <algorithm>
#include <iomanip>
#include <regex>
#include <boost/algorithm/string.hpp>

#include <boost/serialization/nvp.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

#include <boost/multiprecision/cpp_int.hpp>
using boost::multiprecision::cpp_int;

using namespace std;
using namespace ECC;
using namespace beam;
using boost::multiprecision::cpp_dec_float_50;
namespace
{
    string SkipLeadingZeroes(const char* szPtr)
    {
        while (*szPtr == '0')
            szPtr++;

        if (!*szPtr)
            szPtr--; // leave at least 1 symbol

        return szPtr;
    }

    // skips leading zeroes
    template<typename T>
    string EncodeToHex(const T& v)
    {
        char szBuf[sizeof(v) * 2 + 1];
        beam::to_hex(szBuf, &v, sizeof(v));
        return SkipLeadingZeroes(szBuf);
    }

    string EncodeToHex(const ByteBuffer& v)
    {
        if (v.empty())
        {
            return {};
        }
        char* szBuf = (char*)alloca(2 * v.size() + 1);
        beam::to_hex(szBuf, &v[0], v.size());
        return SkipLeadingZeroes(szBuf);
    }

    bool CopyParameter(beam::wallet::TxParameterID paramID, const beam::wallet::TxParameters& input, beam::wallet::TxParameters& dest)
    {
        ByteBuffer buf;
        if (input.GetParameter(paramID, buf))
        {
            dest.SetParameter(paramID, buf);
            return true; // copied
        }
        return false;
    }

    beam::wallet::TxFailureReason GetFailtureReason(const beam::wallet::TxParameters& txParams)
    {
        beam::wallet::TxFailureReason failureReason = beam::wallet::TxFailureReason::Unknown;
        if (auto value = txParams.GetParameter<beam::wallet::TxFailureReason>(beam::wallet::TxParameterID::FailureReason); value)
            failureReason = *value;

        return failureReason;
    }
}

namespace std
{
    string to_string(const beam::wallet::WalletID& id)
    {
        if (id == Zero)
            return {};

        static_assert(sizeof(id) == sizeof(id.m_Channel) + sizeof(id.m_Pk), "");
        return EncodeToHex(id);
    }

    string to_string(const Merkle::Hash& hash)
    {
        if (memis0(hash.m_pData, hash.nBytes))
            return string();

        char sz[Merkle::Hash::nTxtLen + 1];
        hash.Print(sz);
        return string(sz);
    }

    string to_string(const beam::wallet::PrintableAmount& amount)
    {
        std::ostringstream os;
        beam::operator << (os, amount);
        return os.str();
    }

    string to_string(const beam::wallet::TxParameters& value)
    {
        beam::wallet::TxToken token(value);
        Serializer s;
        s & token;
        ByteBuffer buffer;
        s.swap_buf(buffer);
        return beam::wallet::EncodeToBase58(buffer);
    }

    string to_string(const beam::wallet::TxID& id)
    {
        return to_hex(id.data(), id.size());
    }

    string to_string(const beam::PeerID& id)
    {
        return EncodeToHex(id);
    }

    string to_base58(const beam::PeerID& id)
    {
        return Base58::to_string(id);
    }

     unsigned to_unsigned(const std::string& what, bool throws)
     {
        try
        {
            return boost::lexical_cast<unsigned>(what);
        }
        catch(...)
        {
            if (throws) throw;
            return 0;
        }
     }

    string to_string(const beam::AmountBig::Type& amount)
    {
        cpp_int intval;
        import_bits(intval, amount.m_pData, amount.m_pData + beam::AmountBig::Type::nBytes);

        stringstream ss;
        ss << intval;

        return ss.str();
    }

}  // namespace std

namespace beam
{
    std::ostream& operator<<(std::ostream& os, const wallet::TxID& uuid)
    {
        stringstream ss;
        ss << "[" << std::to_string(uuid) << "]";
        os << ss.str();
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const wallet::PrintableAmount& x)
    {
        AmountBig::Print(os, x.m_value);

        if (static_cast<Asset::ID>(-1) != x.m_Aid)
        {
            os << ' ';

            if (x.m_Aid)
            {
                os << kASSET << '-' << x.m_Aid;
                if (x.m_szCoinName && *x.m_szCoinName)
                    os << " (" << x.m_szCoinName << ')';
            }
            else
                os << kBEAM;
        }

        return os;
    }

}  // namespace beam

namespace beam::wallet
{
    const char* get_BroadcastValidatorPublicKey()
    {
        switch (Rules::get().m_Network)
        {
        case Rules::Network::testnet: return "dc3df1d8cd489c3fe990eb8b4b8a58089a7706a5fc3b61b9c098047aac2c2812";
        case Rules::Network::mainnet: return "8ea783eced5d65139bbdf432814a6ed91ebefe8079395f63a13beed1dfce39da";
        case Rules::Network::dappnet: return "4c5b0b58caf69542490d1bef077467010a396cd20a4d1bbba269c8dff41da44e";
        case Rules::Network::masternet: return "db617cedb17543375b602036ab223b67b06f8648de2bb04de047f485e7a9daec";
        }
        return "";
    }

    namespace
    {
        TxAddressType GetAddressTypeImpl(const TxParameters& params)
        {
            auto type = params.GetParameter<TxType>(TxParameterID::TransactionType);
            if (type)
            {
                if (*type == TxType::Simple)
                    return TxAddressType::Regular;

                if (*type == TxType::AtomicSwap)
                    return TxAddressType::AtomicSwap;

                if (*type != TxType::PushTransaction)
                    return TxAddressType::Unknown;
            }

            auto peerAddr = params.GetParameter<WalletID>(TxParameterID::PeerAddr); // sbbs addres
            auto peerEndpoint = params.GetParameter<PeerID>(TxParameterID::PeerEndpoint);

            auto voucher = params.GetParameter<ShieldedTxo::Voucher>(TxParameterID::Voucher);
            if (voucher && peerEndpoint)
                return TxAddressType::MaxPrivacy;

            auto vouchers = params.GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList);
            if (vouchers && !vouchers->empty() && peerEndpoint && peerAddr)
                return TxAddressType::Offline;

            auto gen = params.GetParameter<ShieldedTxo::PublicGen>(TxParameterID::PublicAddreessGen);
            if (gen)
                return TxAddressType::PublicOffline;

            if (peerAddr)
                return TxAddressType::Regular;

            return TxAddressType::Unknown;
        }
    }

    const char kTimeStampFormat3x3[] = "%Y.%m.%d %H:%M:%S";
    const char kTimeStampFormatCsv[] = "%d %b %Y | %H:%M";

    int WalletID::cmp(const WalletID& x) const
    {
        int n = m_Channel.cmp(x.m_Channel);
        if (n)
            return n;
        return m_Pk.cmp(x.m_Pk);
    }

    bool WalletID::FromBuf(const ByteBuffer& x)
    {
        if (x.size() > sizeof(*this))
            return false;

        typedef uintBig_t<sizeof(*this)> BigSelf;
        static_assert(sizeof(BigSelf) == sizeof(*this), "");

        *reinterpret_cast<BigSelf*>(this) = Blob(x);
        return true;
    }

    bool WalletID::FromHex(const std::string& s)
    {
        bool bValid = true;
        ByteBuffer bb = from_hex(s, &bValid);

        return bValid && FromBuf(bb);
    }

    bool WalletID::IsValid() const
    {
        BbsChannel channel;
        m_Channel.Export(channel);
        Point::Native p;
        return m_Pk.ExportNnz(p) && channel < proto::Bbs::s_MaxWalletChannels;
    }

    BbsChannel WalletID::get_Channel() const
    {
        BbsChannel ret;
        m_Channel.Export(ret);
        return ret;
    }

    void WalletID::SetChannelFromPk()
    {
        // derive the channel from the address
        BbsChannel ch;
        m_Pk.ExportWord<0>(ch);
        ch %= proto::Bbs::s_MaxWalletChannels;

        m_Channel = ch;
    }

    boost::optional<PeerID> GetPeerIDFromHex(const std::string& s)
    {
        boost::optional<PeerID> res;
        bool isValid = false;
        auto buf = from_hex(s, &isValid);
        if (!isValid)
        {
            return res;
        }
        res.emplace();
        *res = Blob(buf);
        return res;
    }

    bool fromByteBuffer(const ByteBuffer& b, ByteBuffer& value)
    {
        value = b;
        return true;
    }

    ByteBuffer toByteBuffer(const ECC::Point::Native& value)
    {
        ECC::Point pt;
        if (value.Export(pt))
        {
            return toByteBuffer(pt);
        }
        return ByteBuffer();
    }

    ByteBuffer toByteBuffer(const ECC::Scalar::Native& value)
    {
        ECC::Scalar s;
        value.Export(s);
        return toByteBuffer(s);
    }

    ByteBuffer toByteBuffer(const ByteBuffer& value)
    {
        return value;
    }

    ErrorType getWalletError(proto::NodeProcessingException::Type exceptionType)
    {
        switch (exceptionType)
        {
        case proto::NodeProcessingException::Type::Incompatible:
            return ErrorType::NodeProtocolIncompatible;
        case proto::NodeProcessingException::Type::TimeOutOfSync:
            return ErrorType::TimeOutOfSync;
        default:
            return ErrorType::NodeProtocolBase;
        }
    }

    ErrorType getWalletError(io::ErrorCode errorCode)
    {
        switch (errorCode)
        {
        case io::ErrorCode::EC_ETIMEDOUT:
            return ErrorType::ConnectionTimedOut;
        case io::ErrorCode::EC_ECONNREFUSED:
            return ErrorType::ConnectionRefused;
        case io::ErrorCode::EC_EHOSTUNREACH:
            return ErrorType::ConnectionHostUnreach;
        case io::ErrorCode::EC_EADDRINUSE:
            return ErrorType::ConnectionAddrInUse;
        case io::ErrorCode::EC_HOST_RESOLVED_ERROR:
            return ErrorType::HostResolvedError;
        default:
            return ErrorType::ConnectionBase;
        }
    }

    bool ConfirmationBase::IsValid(const PeerID& pid) const
    {
        Point::Native pk;
        if (!pid.ExportNnz(pk))
            return false;

        Hash::Value hv;
        get_Hash(hv);

        return m_Signature.IsValid(hv, pk);
    }

    void ConfirmationBase::Sign(const Scalar::Native& sk)
    {
        Hash::Value hv;
        get_Hash(hv);

        m_Signature.Sign(hv, sk);
    }

    void PaymentConfirmation::get_Hash(Hash::Value& hv) const
    {
        Hash::Processor hp;
        hp
            << "PaymentConfirmation"
            << m_KernelID
            << m_Sender
            << m_Value;

        if (m_AssetID != Asset::s_InvalidID)
        {
            hp
                << "asset"
                << m_AssetID;
        }

        hp >> hv;
    }

    void SignatureHandler::get_Hash(Hash::Value& hv) const
    {
        beam::Blob data(m_data);
        Hash::Processor()
            << "Undersign"
            << data
            >> hv;
    }

    TxParameters::TxParameters(const boost::optional<TxID>& txID)
        : m_ID(txID)
    {

    }

    bool TxParameters::operator==(const TxParameters& other)
    {
        return m_ID == other.m_ID &&
            m_Parameters == other.m_Parameters;
    }

    bool TxParameters::operator!=(const TxParameters& other)
    {
        return !(*this == other);
    }

    const boost::optional<TxID>& TxParameters::GetTxID() const
    {
        return m_ID;
    }

    TxParameters& TxParameters::SetParameter(TxParameterID parameterID, ByteBuffer&& parameter, SubTxID subTxID)
    {
        m_Parameters[subTxID][parameterID] = std::move(parameter);
        return *this;
    }

    PackedTxParameters TxParameters::Pack() const
    {
        PackedTxParameters parameters;
        for (const auto& subTx : m_Parameters)
        {
            if (subTx.first > kDefaultSubTxID)
            {
                parameters.emplace_back(TxParameterID::SubTxIndex, toByteBuffer(subTx.first));
            }
            for (const auto& p : subTx.second)
            {
                parameters.emplace_back(p.first, p.second);
            }
        }
        return parameters;
    }

    TxToken::TxToken(const TxParameters& parameters)
        : m_Flags(TxToken::TokenFlag)
        , m_TxID(parameters.GetTxID())
        , m_Parameters(parameters.Pack())
    {

    }

    TxParameters TxToken::UnpackParameters() const
    {
        TxParameters result(m_TxID);

        SubTxID subTxID = kDefaultSubTxID;
        Deserializer d;
        for (const auto& p : m_Parameters)
        {
            if (p.first == TxParameterID::SubTxIndex)
            {
                // change subTxID
                d.reset(p.second.data(), p.second.size());
                d & subTxID;
                continue;
            }

            result.SetParameter(p.first, p.second, subTxID);
        }
        return result;
    }

    bool TxToken::IsValid() const
    {
        for (const auto& p : m_Parameters)
        {
            switch (p.first)
            {
#define MACRO(name, index, type) \
            case TxParameterID::name: \
                { \
                    type value; \
                    if (!fromByteBuffer(p.second, value)) \
                    { \
                        return false; \
                    } \
                } break; 
                BEAM_TX_PUBLIC_PARAMETERS_MAP(MACRO)
#undef MACRO
            default:
                break;
            }
        }
        return true;
    }

    boost::optional<TxParameters> ParseParameters(const string& text)
    {
        bool isValid = true;
        ByteBuffer buffer = from_hex(text, &isValid);
        if (!isValid)
        {
            buffer = DecodeBase58(text);
            if (buffer.empty())
            {
                return {};
            }
        }

        if (buffer.size() < 2)
        {
            return {};
        }

        if (buffer.size() > 33 && buffer[0] & TxToken::TokenFlag) // token
        {
            try
            {
                TxToken token;
                // simply deserialize for now
                Deserializer d;
                d.reset(&buffer[0], buffer.size());
                d & token;

                if (token.IsValid())
                {
                    return boost::make_optional<TxParameters>(token.UnpackParameters());
                }
            }
            catch (...)
            {
                // failed to deserialize
            }
        }
        else // plain WalletID
        {
            WalletID walletID;
            if (walletID.FromBuf(buffer) && walletID.IsValid())
            {
                auto result = boost::make_optional<TxParameters>({});
                result->SetParameter(TxParameterID::PeerAddr, walletID);
                return result;
            }
        }
        return {};
    }

    bool LoadReceiverParams(const TxParameters& receiverParams, TxParameters& params, TxAddressType type)
    {
        const TxParameters& p = receiverParams;
        switch (type)
        {
        case TxAddressType::AtomicSwap:
        case TxAddressType::Regular:
            {
                if (type == TxAddressType::Regular)
                {
                    params.SetParameter(TxParameterID::TransactionType, TxType::Simple);
                }
                else if (type == TxAddressType::AtomicSwap)
                {
                    params.SetParameter(TxParameterID::TransactionType, TxType::AtomicSwap);
                }

                if (!CopyParameter(TxParameterID::PeerAddr, p, params))
                {
                    return false;
                }
                CopyParameter(TxParameterID::PeerEndpoint, p, params);
                break;
            }

        case TxAddressType::PublicOffline:
            {
                params.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction);

                ShieldedTxo::PublicGen pgen;
                if (!p.GetParameter(TxParameterID::PublicAddreessGen, pgen))
                    return false;

                PeerID pid;
                ECC::Signature sig;
                if (p.GetParameter(TxParameterID::PublicAddressGenSig, sig) && p.GetParameter(TxParameterID::PeerEndpoint, pid))
                {
                    params.SetParameter(TxParameterID::PublicAddreessGen, pgen);
                    params.SetParameter(TxParameterID::PublicAddressGenSig, sig);
                    params.SetParameter(TxParameterID::PeerEndpoint, pid);
                }
                else
                {
                    // legacy address. Temporarily support them.
                    ECC::Hash::Value hv;
                    ECC::GenRandom(hv); // nonce

                    ShieldedTxo::Voucher voucher;

                    ShieldedTxo::Data::TicketParams tp;
                    tp.Generate(voucher.m_Ticket, pgen, hv);

                    voucher.m_SharedSecret = tp.m_SharedSecret;

                    // fake Endpoint
                    Scalar::Native sk;
                    sk.GenRandomNnz();
                    pid.FromSk(sk);

                    voucher.get_Hash(hv);
                    voucher.m_Signature.Sign(hv, sk);

                    params.SetParameter(TxParameterID::Voucher, voucher);
                    params.SetParameter(TxParameterID::PeerEndpoint, pid);
                }
            }
            break;

        case TxAddressType::Offline:
            {
                params.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction);
                CopyParameter(TxParameterID::PeerAddr, p, params);
                auto peerEndpoint = p.GetParameter<PeerID>(TxParameterID::PeerEndpoint);
                params.SetParameter(TxParameterID::PeerEndpoint, *peerEndpoint);

                if (auto vouchers = p.GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList); vouchers)
                {
                    if (!IsValidVoucherList(*vouchers, *peerEndpoint))
                    {
                        BEAM_LOG_ERROR() << "Voucher signature verification failed. Unauthorized voucher was provider.";
                        return false;
                    }
                    params.SetParameter(TxParameterID::ShieldedVoucherList, *vouchers);
                }
            }
            break;

        case TxAddressType::MaxPrivacy:
            {
                params.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction);
                auto peerEndpoint = p.GetParameter<PeerID>(TxParameterID::PeerEndpoint);
                params.SetParameter(TxParameterID::PeerEndpoint, *peerEndpoint);
                auto voucher = p.GetParameter<ShieldedTxo::Voucher>(TxParameterID::Voucher);
                if (!voucher->IsValid(*peerEndpoint))
                {
                    BEAM_LOG_ERROR() << "Voucher signature verification failed. Unauthorized voucher was provider.";
                    return false;
                }
                params.SetParameter(TxParameterID::Voucher, *voucher);
                params.SetParameter(TxParameterID::MaxPrivacyMinAnonimitySet, uint8_t(64));
            }
            break;

        default:
            return false;
        }

        params.SetParameter(TxParameterID::AddressType, type);
        ProcessLibraryVersion(receiverParams);

        return true;
    }

    bool IsValidTimeStamp(Timestamp currentBlockTime_s, Timestamp tolerance_s)
    {
        Timestamp currentTime_s = getTimestamp();

        if (currentTime_s > currentBlockTime_s + tolerance_s)
        {
            BEAM_LOG_INFO() << "It seems that last known blockchain tip is not up to date";
            return false;
        }
        return true;
    }

    std::tuple<TxStatus, bool, bool> ParseParamsForStatusInterpretation(const TxParameters& txParams)
    {
        TxStatus status = TxStatus::Pending;
        bool sender = false;
        bool selfTx = false;

        if (auto value = txParams.GetParameter<wallet::TxStatus>(TxParameterID::Status); value)
            status = *value;

        if (auto value = txParams.GetParameter<bool>(TxParameterID::IsSender); value)
            sender = *value;

        if (auto value = txParams.GetParameter<bool>(TxParameterID::IsSelfTx); value)
            selfTx = *value;

        return std::make_tuple(status, sender, selfTx);
    }

    std::string GetTxStatusStr(const TxParameters& txParams)
    {
        auto [status, sender, selfTx] = ParseParamsForStatusInterpretation(txParams);

        switch (status)
        {
            case TxStatus::Pending:
                return "pending";
            case TxStatus::InProgress:
                return selfTx  ? "self sending" : (sender ? "waiting for receiver" : "waiting for sender");
            case TxStatus::Registering: 
                return selfTx ? "self sending" : (sender ? "sending" : "receiving" );
            case TxStatus::Failed:
                return TxFailureReason::TransactionExpired == GetFailtureReason(txParams) ? "expired" : "failed";
            case TxStatus::Canceled:
                return "cancelled";
            case TxStatus::Completed:
                return selfTx ? "completed" : (sender ? "sent" : "received");
            case TxStatus::Confirming:
                return "confirming";
            default:
                BOOST_ASSERT_MSG(false, kErrorUnknownTxStatus);
                return "unknown";
        }
    }

    std::string GetSimpleTxStatusStr(const TxParameters& txParams)
    {
        auto [status, sender, selfTx] = ParseParamsForStatusInterpretation(txParams);

        switch (status)
        {
        case TxStatus::InProgress:
            return selfTx ? "sending to own address" : (sender ? "waiting for receiver" : "waiting for sender");
        case TxStatus::Registering:
            return selfTx ? "sending to own address" : "in progress";
        case TxStatus::Completed:
            return selfTx ? "sent to own address" : (sender ? "sent" : "received");
        default:
            break;
        }
        return GetTxStatusStr(txParams);
    }

    std::string GetMaxAnonimityTxStatusStr(const TxParameters& txParams)
    {
        TxAddressType addressType = TxAddressType::Unknown;
        auto storedType = txParams.GetParameter<TxAddressType>(TxParameterID::AddressType);
        if (storedType)
        {
            addressType = *storedType;
        }
        else
        {
            TxDescription tx_desc(txParams);
            addressType = tx_desc.m_sender ? GetAddressType(tx_desc) : TxAddressType::Unknown;
        }

        auto [status, sender, selfTx] = ParseParamsForStatusInterpretation(txParams);

        switch (addressType)
        {
            case TxAddressType::MaxPrivacy:
                switch (status)
                {
                case TxStatus::Registering:
                    return selfTx ? "sending maximum anonymity to own address" : "in progress maximum anonymity";
                case TxStatus::Failed:
                    return TxFailureReason::TransactionExpired == GetFailtureReason(txParams) ? "expired" : "failed maximum anonymity";
                case TxStatus::Canceled:
                    return "canceled maximum anonymity";
                case TxStatus::Completed:
                    return selfTx ? "sent maximum anonymity to own address" : (sender ? "sent maximum anonymity" : "received maximum anonymity");
                default:
                    break;
                }
                break;
            case TxAddressType::Offline:
                switch (status)
                {
                case TxStatus::Registering:
                    return selfTx ? "sending offline to own address" : "in progress offline";
                case TxStatus::Failed:
                    return TxFailureReason::TransactionExpired == GetFailtureReason(txParams) ? "expired" : "failed offline";
                case TxStatus::Canceled:
                    return "canceled offline";
                case TxStatus::Completed:
                    return selfTx ? "sent offline to own address" : (sender ? "sent offline" : "received offline");
                default:
                    break;
                }
                break;
            case TxAddressType::PublicOffline:
                switch (status)
                {
                case TxStatus::Registering:
                    return selfTx ? "sending public offline to own address" : "in progress public offline";
                case TxStatus::Failed:
                    return TxFailureReason::TransactionExpired == GetFailtureReason(txParams) ? "expired" : "failed public offline";
                case TxStatus::Canceled:
                    return "canceled public offline";
                case TxStatus::Completed:
                    return selfTx ? "sent public offline to own address" : (sender ? "sent public offline" : "received public offline");
                default:
                    break;
                }
                break;
            default:
                break;
        }

        return GetTxStatusStr(txParams);
    }

    std::string GetAssetTxStatusStr(const TxParameters& txParams)
    {
        wallet::TxType txType = wallet::TxType::AssetInfo;
        if (auto value = txParams.GetParameter<TxType>(TxParameterID::TransactionType); value)
            txType = *value;

        TxStatus status = TxStatus::Pending;
        if (auto value = txParams.GetParameter<wallet::TxStatus>(TxParameterID::Status); value)
            status = *value;

        if (status == TxStatus::InProgress && txType == TxType::AssetInfo) return "getting info";
        if (status == TxStatus::Completed)
        {
            switch (txType)
            {
                case TxType::AssetIssue: return "asset issued";
                case TxType::AssetConsume: return "asset consumed";
                case TxType::AssetReg: return "asset registered";
                case TxType::AssetUnreg: return "asset unregistered";
                case TxType::AssetInfo: return "asset confirmed";
                default: break;
            }
        }

        return GetTxStatusStr(txParams);
    }

    std::string GetContractTxStatusStr(const TxParameters& txParams)
    {
        TxStatus status = TxStatus::Pending;
        if (auto value = txParams.GetParameter<wallet::TxStatus>(TxParameterID::Status); value)
            status = *value;

        switch (status)
        {
            case TxStatus::InProgress:
            case TxStatus::Registering:
                return "in progress";
            case TxStatus::Completed:
                return "completed";
            default:
                break;
        }
        return GetTxStatusStr(txParams);
    }

    std::string interpretStatus(const TxDescription& tx)
    {
        switch (tx.m_txType)
        {
            case wallet::TxType::Simple:
                return GetSimpleTxStatusStr(tx);
            case wallet::TxType::AssetIssue:
            case wallet::TxType::AssetConsume:
            case wallet::TxType::AssetReg:
            case wallet::TxType::AssetUnreg:
            case wallet::TxType::AssetInfo:
                return GetAssetTxStatusStr(tx);
            case wallet::TxType::PushTransaction:
                return GetMaxAnonimityTxStatusStr(tx);
            case wallet::TxType::Contract:
                return GetContractTxStatusStr(tx);
            case wallet::TxType::DexSimpleSwap:
                return GetSimpleTxStatusStr(tx);
            default:
                BOOST_ASSERT_MSG(false, kErrorUnknownTxType);
                return "unknown";
        }
    }

    TxDescription::TxDescription(const TxParameters& p)
        : TxParameters(p)
    {
        fillFromTxParameters(*this);
    }

    void TxDescription::fillFromTxParameters(const TxParameters& parameters)
    {
        boost::optional<TxID> txId = parameters.GetTxID();
        if (txId)
        {
            m_txId = *txId;
        }

        #define MACRO(id, type, field, init) \
        if (auto value = parameters.GetParameter<type>(id); value) \
            field = *value; 
        BEAM_TX_DESCRIPTION_INITIAL_PARAMS(MACRO)
        #undef MACRO
    }

    bool TxDescription::canResume() const
    {
        return m_status == TxStatus::Pending
            || m_status == TxStatus::InProgress
            || m_status == TxStatus::Registering
            || m_status == TxStatus::Confirming;
    }

    bool TxDescription::canCancel() const
    {
        return m_status == TxStatus::InProgress
            || m_status == TxStatus::Pending;
    }

    bool TxDescription::canDelete() const
    {
        return m_status == TxStatus::Failed
            || m_status == TxStatus::Completed
            || m_status == TxStatus::Canceled;
    }

    std::string TxDescription::getTxTypeString() const
    {
        switch(m_txType)
        {
#define MACRO(type, index, s) case TxType::type: return s;
            BEAM_TX_TYPES_MAP(MACRO)
#undef MACRO

        default:
            BOOST_ASSERT_MSG(false, kErrorUnknownTxType);
            return "unknown";
        }
    }

    /// Return empty string if exchange rate is not available
    Amount TxDescription::getExchangeRate(const Currency& targetCurrency, beam::Asset::ID assetId /* = beam::Asset::s_InvalidID*/) const
    {
        auto exchangeRatesOptional = GetParameter<std::vector<ExchangeRate>>(TxParameterID::ExchangeRates);
        if (exchangeRatesOptional)
        {
            std::vector<ExchangeRate>& rates = *exchangeRatesOptional;

            beam::wallet::Currency fromCurrency(beam::Asset::s_BeamID);
            if (assetId == beam::Asset::s_InvalidID || assetId == beam::Asset::s_BeamID)
            {
                auto assetIdOptional = GetParameter<Asset::ID>(TxParameterID::AssetID);
                Asset::ID txAssetId  = assetIdOptional ? *assetIdOptional : beam::Asset::s_BeamID;
                fromCurrency = beam::wallet::Currency(txAssetId);
            } else
            {
                fromCurrency = beam::wallet::Currency(assetId);
            }

            auto search = std::find_if(std::begin(rates), std::end(rates),
                                    [&fromCurrency, &targetCurrency](const ExchangeRate& r)
                                    {
                                        return r.m_from == fromCurrency && r.m_to == targetCurrency;
                                    });

            if (search != std::cend(rates))
            {
                return search->m_rate;
            }
        }

        return 0UL;
    }

    std::string TxDescription::getToken() const
    {
        auto token = GetParameter<std::string>(TxParameterID::OriginalToken);
        if (token)
        {
            return *token;
        }
        return {};
    }

    std::string TxDescription::getSenderEndpoint() const
    {
        return getEndpoint(m_sender);
    }

    std::string TxDescription::getReceiverEndpoint() const
    {
        return getEndpoint(!m_sender);
    }

    std::string TxDescription::getSender() const
    {
        return std::to_string(m_sender ? m_myAddr : m_peerAddr);
    }

    std::string TxDescription::getReceiver() const
    {
        return std::to_string(!m_sender ? m_myAddr : m_peerAddr);
    }

    std::string TxDescription::getEndpoint(bool isMy) const
    {
        auto v = GetParameter<PeerID>(isMy ? TxParameterID::MyEndpoint : TxParameterID::PeerEndpoint);
        if (v)
            return std::to_base58(*v);

        // try from addr
        const WalletID& wid = isMy ? m_myAddr : m_peerAddr;
        if (wid.m_Pk != Zero)
            return std::to_base58(wid.m_Pk);

        return {};
    }

    std::string TxDescription::getAddressFrom() const
    {
        if (m_txType == wallet::TxType::PushTransaction && !m_sender)
        {
            return getSenderEndpoint();
        }
        return std::to_string(m_sender ? m_myAddr : m_peerAddr);
    }

    std::string TxDescription::getAddressTo() const
    {
        if (m_sender)
        {
            auto token = getToken();
            if (token.empty())
                return std::to_string(m_peerAddr);

            return token;
        }
        return std::to_string(m_myAddr);
    }

    uint64_t get_RandomID()
    {
        uintBigFor<uint64_t>::Type val;
        ECC::GenRandom(val);

        uint64_t ret;
        val.Export(ret);
        return ret;
    }

    std::string GetSendToken(const std::string& sbbsAddress, const std::string& endpointStr, Amount amount)
    {
        WalletID walletID;
        if (!walletID.FromHex(sbbsAddress))
        {
            return "";
        }
        auto endPoint = GetPeerIDFromHex(endpointStr);
        if (!endPoint)
        {
            return "";
        }

        TxParameters parameters;
        if (amount > 0)
        {
            parameters.SetParameter(TxParameterID::Amount, amount);
        }

        parameters.SetParameter(TxParameterID::PeerAddr, walletID);
        parameters.SetParameter(TxParameterID::TransactionType, beam::wallet::TxType::Simple);
        parameters.SetParameter(TxParameterID::PeerEndpoint, *endPoint);

        return std::to_string(parameters);
    }

    ShieldedVoucherList GenerateVoucherList(const std::shared_ptr<IPrivateKeyKeeper2>& pKeeper, uint64_t ownID, size_t count)
    {
        ShieldedVoucherList res;

        if (pKeeper && count)
        {
            IPrivateKeyKeeper2::Method::CreateVoucherShielded m;
            m.m_iEndpoint = ownID;
            m.m_Count = static_cast<uint32_t>(count);
            ECC::GenRandom(m.m_Nonce);

            if (IPrivateKeyKeeper2::Status::Success == pKeeper->InvokeSync(m))
                res = std::move(m.m_Res);
        }

        return res;
    }

    bool IsValidVoucherList(const ShieldedVoucherList& vouchers, const PeerID& identity)
    {
        if (vouchers.empty())
            return false;

        ECC::Point::Native pk;
        if (!identity.ExportNnz(pk))
            return false;

        for (const auto& voucher : vouchers)
        {
            if (!voucher.IsValid(pk))
            {
                return false;
            }
        }
        return true;
    }

    using nlohmann::json;

#define BEAM_IGNORED_JSON_TYPES2(MACRO) \
        MACRO(ECC::RangeProof::Confidential::Part2) \
        MACRO(ECC::RangeProof::Confidential::Part3)

#define MACRO(type) \
    static bool fromByteBuffer(const ByteBuffer&, type&) \
    { \
        return false; \
    } \

    BEAM_IGNORED_JSON_TYPES2(MACRO)
#undef MACRO

#define MACRO(type) \
    static ByteBuffer toByteBuffer(const type&) \
    { \
        return {}; \
    } \

    BEAM_IGNORED_JSON_TYPES2(MACRO)
#undef MACRO

    namespace
    {
        using Names = std::array<string, uint8_t(128)>;
        Names GetParameterNames()
        {
            Names names;
#define MACRO(name, index, type) names[index] = #name;
            BEAM_TX_PUBLIC_PARAMETERS_MAP(MACRO)
#undef MACRO
            return names;
        }

        using NameIDs = std::map<string, TxParameterID>;
        NameIDs GetParameterNameIDs()
        {
            NameIDs names;
#define MACRO(name, index, type) names[#name] = TxParameterID(index);
            BEAM_TX_PUBLIC_PARAMETERS_MAP(MACRO)
#undef MACRO
            return names;
        }

        template<typename T>
        string ToJsonValue(const T& v)
        {
            return std::to_string(v);
        }

        json ToJsonValue(TxType v)
        {
            return uint8_t(v);
        }

        json ToJsonValue(bool v)
        {
            return v;
        }

        string ToJsonValue(const ByteBuffer& buf)
        {
            return EncodeToHex(buf);
        }

        json ToJsonValue(const AmountList& amountList)
        {
            json list = json::array();
            for (const auto& a : amountList)
            {
                list.push_back(std::to_string(a));
            }
            return list;
        }

        json ToJsonValue(const CoinIDList& coinList)
        {
            json list = json::array();
            for (const auto& c : coinList)
            {
                list.push_back(toString(c));
            }
            return list;
        }

#define BEAM_IGNORED_JSON_TYPES(MACRO) \
        MACRO(ECC::Point) \
        MACRO(ECC::Scalar) \
        MACRO(ECC::Signature) \
        MACRO(std::vector<Input::Ptr>) \
        MACRO(std::vector<Output::Ptr>) \
        MACRO(std::vector<ExchangeRate>) \
        MACRO(ShieldedTxo::Voucher) \
        MACRO(ShieldedVoucherList) \
        MACRO(ShieldedTxo::PublicGen)

#define MACRO(type) \
        json ToJsonValue(const type&) \
        { \
            return {}; \
        } \

        BEAM_IGNORED_JSON_TYPES(MACRO)
        BEAM_IGNORED_JSON_TYPES2(MACRO)

#undef MACRO

#define MACRO(type) \
        bool FromJson(const json&, type&) \
        { \
            return false; \
        } \

        BEAM_IGNORED_JSON_TYPES(MACRO)
        BEAM_IGNORED_JSON_TYPES2(MACRO)
#undef MACRO

        const string& ToJsonValue(const std::string& s)
        {
            return s;
        }

        const string& GetParameterName(TxParameterID id)
        {
            static auto names = GetParameterNames();
            return names[uint8_t(id)];
        }

        TxParameterID GetParameterID(const std::string& n)
        {
            static auto names = GetParameterNameIDs();
            return names[n];
        }

        json GetParameterValue(TxParameterID id, const ByteBuffer& buffer)
        {
            switch (id)
            {
#define MACRO(name, index, type) \
            case TxParameterID::name: \
                { \
                    type value; \
                    if (fromByteBuffer(buffer, value)) \
                    { \
                        return ToJsonValue(value); \
                    } \
                } break; 
             BEAM_TX_PUBLIC_PARAMETERS_MAP(MACRO)
#undef MACRO
            default:
                break;
            }
            return {};
        }

        bool FromJson(const json& s, WalletID& v)
        {
            if (!s.is_string())
            {
                return false;
            }
            return v.FromHex(s.get<string>());
        }

        bool FromJson(const json& s, DexOrderID& v)
        {
            if (!s.is_string())
            {
                return false;
            }
            return v.FromHex(s.get<string>());
        }

        template<typename T>
        bool FromJson(const json& s, T& v)
        {
            if (!s.is_string())
            {
                return false;
            }
            v = T(stoll(s.get<string>()));
            return true;
        }

        bool FromJson(const json& s, TxType& v)
        {
            if (!s.is_number_integer())
            {
                return false;
            }

            v = TxType(s.get<uint8_t>());
            return true;
        }

        bool FromJson(const json& s, bool& v)
        {
            if (!s.is_boolean())
            {
                return false;
            }

            v = s.get<bool>();
            return true;
        }

        bool FromJson(const json& s, uintBig& v)
        {
            if (!s.is_string())
            {
                return false;
            }
            bool isValid = false;
            auto buf = from_hex(s.get<std::string>(), &isValid);
            if (!isValid)
            {
                return false;
            }
            v = Blob(buf.data(), static_cast<uint32_t>(buf.size()));
            return true;
        }

        bool FromJson(const json& s, PeerID& p)
        {
            if (!s.is_string())
            {
                return false;
            }
            return FromJson(s, static_cast<uintBig&>(p));
        }

        bool FromJson(const json& s, CoinID& v)
        {
            if (!s.is_string())
            {
                return false;
            }
            auto t = Coin::FromString(s.get<std::string>());
            if (!t)
            {
                return false;
            }
            v = *t;
            return true;
        }

        bool FromJson(const json& s, std::string& v)
        {
            if (!s.is_string())
            {
                return false;
            }
            v = s.get<std::string>();
            return true;
        }

        bool FromJson(const json& s, ByteBuffer& v)
        {
            if (!s.is_string())
            {
                return false;
            }
            bool isValid = false;
            v = from_hex(s.get<std::string>(), &isValid);
            return isValid;
        }

        template<typename T>
        bool VectorFromJson(const json& s, T& v)
        {
            if (!s.is_array())
            {
                return false;
            }
            for (const auto& item : s)
            {
                typename T::value_type t;
                if (FromJson(item, t))
                {
                    v.push_back(t);
                }
            }
            return true;
        }

        bool FromJson(const json& s, AmountList& v)
        {
            return VectorFromJson(s, v);
        }

        bool FromJson(const json& s, CoinIDList& v)
        {
            return VectorFromJson(s, v);
        }

        ByteBuffer GetParameterValue(TxParameterID id, const json& jsonValue)
        {
            switch (id)
            {
#define MACRO(name, index, type) \
            case TxParameterID::name: \
                { \
                    type value; \
                    if (FromJson(jsonValue, value)) \
                    { \
                        return toByteBuffer(value); \
                    } \
                } break; 
                BEAM_TX_PUBLIC_PARAMETERS_MAP(MACRO)
#undef MACRO
            default:
                break;
            }
            return {};
        }

        const std::string ParamsName = "params";
        const std::string TxIDName = "tx_id";
        const std::string IdName = "id";
        const std::string ValueName = "value";
    }

    std::string ConvertTokenToJson(const std::string& token)
    {
        auto p = ParseParameters(token);
        if (!p)
        {
            return {};
        }
        auto packedParams = p->Pack();
        json params = json::object();

        for (const auto& pair : packedParams)
        {
            const auto& v = GetParameterValue(pair.first, pair.second);
            if (v.is_null())
            {
                continue;
            }
            params.push_back(
            {
                GetParameterName(pair.first),
                v
            });
        }
        auto txID = p->GetTxID();

        json res = json
        {
            { TxIDName, txID.is_initialized() ? json(std::to_string(*txID)) : json{}},
            { ParamsName, params }
        };
        return res.dump();
    }

    std::string ConvertJsonToToken(const std::string& jsonParams)
    {
        try
        {
            json obj = json::parse(jsonParams.data(), jsonParams.data() + jsonParams.size());
            auto paramsIt = obj.find(ParamsName);
            if (paramsIt == obj.end())
            {
                return {};
            }
            boost::optional<TxID> txID;
            auto txIDIt = obj.find(TxIDName);
            if (txIDIt != obj.end() && !txIDIt->is_null())
            {
                auto txIdVec = from_hex(*txIDIt);

                if (txIdVec.size() >= 16)
                {
                    txID.emplace();
                    std::copy_n(txIdVec.begin(), 16, txID->begin());
                }
            }
            TxParameters txParams(txID);
            for (const auto& p : paramsIt->items())
            {
                auto id = GetParameterID(p.key());
                ByteBuffer value = GetParameterValue(id, p.value());
                txParams.SetParameter(id, value);
            }
            return std::to_string(txParams);
        }
        catch (const nlohmann::detail::exception&)
        {
        }
        return {};
    }

    std::string TimestampFile(const std::string& fileName)
    {
        size_t dotPos = fileName.find_last_of('.');

        stringstream ss;
        ss << fileName.substr(0, dotPos);
        ss << getTimestamp();

        if (dotPos != string::npos)
        {
            ss << fileName.substr(dotPos);
        }

        string timestampedPath = ss.str();
        return timestampedPath;
    }

    bool g_AssetsEnabled = false; // OFF by default

    TxFailureReason CheckAssetsEnabled(Height h)
    {
        const Rules& r = Rules::get();
        if (h < r.pForks[2].m_Height)
            return TxFailureReason::AssetsDisabledFork2;

        if (!r.CA.Enabled)
            return TxFailureReason::AssetsDisabledInRules;

        if (!g_AssetsEnabled)
            return TxFailureReason::AssetsDisabledInWallet;

        return TxFailureReason::Count;
    }

    bool isFork3(Height h)
    {
        const Rules& r = Rules::get();
        if (h < r.pForks[3].m_Height)
        {
            return false;
        }
        return true;
    }

    void AppendLibraryVersion(TxParameters& params)
    {
#ifdef BEAM_LIB_VERSION
        Version v;
        v.from_string(BEAM_LIB_VERSION);
        auto b = toByteBuffer(v);
        params.SetParameter(beam::wallet::TxParameterID::LibraryVersion, std::string(b.begin(), b.end()));
#endif // BEAM_LIB_VERSION
    }

    void ProcessLibraryVersion(const TxParameters& params, VersionFunc&& func)
    {
#ifdef BEAM_LIB_VERSION
        if (auto libVersion = params.GetParameter<std::string>(TxParameterID::LibraryVersion); libVersion)
        {
            std::string myLibVersionStr = BEAM_LIB_VERSION;
            Version version;
            Version myVersion;
            if (myVersion.from_string(BEAM_LIB_VERSION) && 
                version.from_string(*libVersion) &&
                myVersion < version)
            {
                auto versionStr = std::to_string(version);
                if (func)
                {
                    func(versionStr, myLibVersionStr);
                }
                else
                {
                    BEAM_LOG_WARNING() <<
                        "This address generated by newer Beam library version(" << versionStr << ")\n" <<
                        "Your version is: " << myLibVersionStr << " Please, check for updates.";
                }
            }
        }
#endif // BEAM_LIB_VERSION
    }

    void ProcessClientVersion(const TxParameters& params, const std::string& appName, const std::string& myClientVersion, const std::string& libVersion, VersionFunc&& func)
    {
        if (auto clientVersion = params.GetParameter<std::string>(TxParameterID::ClientVersion); clientVersion)
        {
            auto res = clientVersion->find(appName + " ");
            if (res != std::string::npos)
            {
                clientVersion->erase(0, appName.length() + 1);
                std::regex clientVersionRegex("\\d{1,}\\.\\d{1,}\\.\\d{4,}\\.\\d{4,}");
                if (std::regex_match(*clientVersion, clientVersionRegex) &&
                    std::lexicographical_compare(
                        myClientVersion.begin(),
                        myClientVersion.end(),
                        clientVersion->begin(),
                        clientVersion->end(),
                        std::less<char>{}))
                {
                    func(*clientVersion, myClientVersion);
                    return;
                }
                return;
            }
            // attempt to read packed version
            auto pos =  myClientVersion.rfind('.');
            if (pos == std::string::npos)
            {
                // invalid my version
                return;
            }
            char* endPos = nullptr;
            uint32_t clientRevision = std::strtoul(myClientVersion.c_str() + pos, &endPos, 10);
            ClientVersion cv;
            if (!fromByteBuffer(ByteBuffer(clientVersion->begin(), clientVersion->end()), cv))
            {
                // failed to deserialize, ignore
                return;
            }

            ClientVersion myCv(clientRevision);
            if (myCv < cv)
            {
                std::string versionStr = libVersion;
                versionStr.append(".");
                versionStr.append(std::to_string(cv.m_revision));
                func(versionStr, myClientVersion);
            }
        }
    }

    uint32_t GetShieldedInputsNum(const std::vector<TxKernel::Ptr>& v)
    {
        uint32_t ret = 0;
        for (uint32_t i = 0; i < v.size(); i++)
            if (TxKernel::Subtype::ShieldedInput == v[i]->get_Subtype())
                ret++;
        return ret;
    }

    TxAddressType GetAddressType(const TxDescription& tx)
    {
        return GetAddressTypeImpl(tx);
    }

    TxAddressType GetAddressType(const std::string& address)
    {
        auto p = ParseParameters(address);
        if (!p)
            return TxAddressType::Unknown;

        return GetAddressTypeImpl(*p);
    }
}  // namespace beam::wallet
