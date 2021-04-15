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
#include "core/serialization_adapters.h"
#include "base58.h"
#include "utility/string_helpers.h"
#include "strings_resources.h"
#include "core/shielded.h"
#include "3rdparty/nlohmann/json.hpp"
#include "wallet_db.h"

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
        cpp_int intval;
        import_bits(intval, amount.m_value.m_pData, amount.m_value.m_pData + decltype(amount.m_value)::nBytes);

        if (amount.m_showPoint)
        {
            const auto maxGroths = std::lround(std::log10(Rules::Coin));
            cpp_dec_float_50 floatval(intval);

            stringstream ss;
            ss << fixed << setprecision(maxGroths) << floatval / Rules::Coin;
            auto str   = ss.str();
            const auto point = std::use_facet< std::numpunct<char>>(ss.getloc()).decimal_point();

            boost::algorithm::trim_right_if(str, boost::is_any_of("0"));
            boost::algorithm::trim_right_if(str, [point](const char ch) {return ch == point;});

            return str;
        }
        else
        {
            stringstream ss;
            cpp_int coin  = intval / Rules::Coin;
            cpp_int groth = intval - coin * Rules::Coin;

            if (intval >= Rules::Coin)
            {
                ss << coin << " " << (amount.m_coinName.empty() ? "BEAMs" : amount.m_coinName);
            }

            if (groth > 0 || intval == 0)
            {
                ss << (intval >= Rules::Coin ? (" ") : "")
                   << groth << " " << (amount.m_grothName.empty() ? "GROTH" : amount.m_grothName);
            }

            return ss.str();
        }
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

    string to_string(const beam::Version& v)
    {
        return v.to_string();
    }

    string to_string(const beam::wallet::TxID& id)
    {
        return to_hex(id.data(), id.size());
    }

    string to_string(const beam::PeerID& id)
    {
        return EncodeToHex(id);
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

    std::ostream& operator<<(std::ostream& os, const wallet::PrintableAmount& amount)
    {
        os << std::to_string(amount);
        
        return os;
    }
}  // namespace beam

namespace beam::wallet
{
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

            auto peerID = params.GetParameter<WalletID>(TxParameterID::PeerID); // sbbs addres
            auto peerIdentity = params.GetParameter<PeerID>(TxParameterID::PeerWalletIdentity); // identity

            auto voucher = params.GetParameter<ShieldedTxo::Voucher>(TxParameterID::Voucher);
            if (voucher && peerIdentity)
                return TxAddressType::MaxPrivacy;

            auto vouchers = params.GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList);
            if (vouchers && !vouchers->empty() && peerIdentity && peerID)
                return TxAddressType::Offline;

            auto gen = params.GetParameter<ShieldedTxo::PublicGen>(TxParameterID::PublicAddreessGen);
            if (gen)
                return TxAddressType::PublicOffline;

            if (peerID)
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

    void SwapOfferConfirmation::get_Hash(Hash::Value& hv) const
    {
        beam::Blob data(m_offerData);
        Hash::Processor()
            << "SwapOfferSignature"
            << data
            >> hv;
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
                result->SetParameter(TxParameterID::PeerID, walletID);
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

                if (!CopyParameter(TxParameterID::PeerID, p, params))
                {
                    return false;
                }
                CopyParameter(TxParameterID::PeerWalletIdentity, p, params);
                break;
            }

        case TxAddressType::PublicOffline:
            {
                params.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction);
                auto publicGen = p.GetParameter<ShieldedTxo::PublicGen>(TxParameterID::PublicAddreessGen);
                // generate fake peerID
                Scalar::Native sk;
                sk.GenRandomNnz();
                PeerID pid;  // fake peedID
                pid.FromSk(sk);
                ShieldedTxo::Voucher voucher = GenerateVoucherFromPublicAddress(*publicGen, sk);
                params.SetParameter(TxParameterID::Voucher, voucher);
                params.SetParameter(TxParameterID::PeerWalletIdentity, pid);
            }
            break;

        case TxAddressType::Offline:
            {
                params.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction);
                CopyParameter(TxParameterID::PeerID, p, params);
                CopyParameter(TxParameterID::PeerOwnID, p, params);
                auto peerID = p.GetParameter<PeerID>(TxParameterID::PeerWalletIdentity); 
                params.SetParameter(TxParameterID::PeerWalletIdentity, *peerID);

                if (auto vouchers = p.GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList); vouchers)
                {
                    if (!IsValidVoucherList(*vouchers, *peerID))
                    {
                        LOG_ERROR() << "Voucher signature verification failed. Unauthorized voucher was provider.";
                        return false;
                    }
                    params.SetParameter(TxParameterID::ShieldedVoucherList, *vouchers);
                }
            }
            break;

        case TxAddressType::MaxPrivacy:
            {
                params.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction);
                CopyParameter(TxParameterID::PeerOwnID, p, params);
                auto peerID = p.GetParameter<PeerID>(TxParameterID::PeerWalletIdentity);
                params.SetParameter(TxParameterID::PeerWalletIdentity, *peerID);
                auto voucher = p.GetParameter<ShieldedTxo::Voucher>(TxParameterID::Voucher);
                if (!voucher->IsValid(*peerID))
                {
                    LOG_ERROR() << "Voucher signature verification failed. Unauthorized voucher was provider.";
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
            LOG_INFO() << "It seems that last known blockchain tip is not up to date";
            return false;
        }
        return true;
    }

    TxStatusInterpreter::TxStatusInterpreter(const TxParameters& txParams)
    {
        if (auto value = txParams.GetParameter<wallet::TxStatus>(TxParameterID::Status); value)
        {
            m_status = *value;
            if (m_status == TxStatus::Failed)
            {
                if (auto value2 = txParams.GetParameter<TxFailureReason>(TxParameterID::FailureReason); value2)
                    m_failureReason = *value2;
            }
        }

        if (auto value = txParams.GetParameter<bool>(TxParameterID::IsSender); value)
            m_sender = *value;

        if (auto value = txParams.GetParameter<bool>(TxParameterID::IsSelfTx); value)
            m_selfTx = *value;
    }

    std::string TxStatusInterpreter::getStatus() const
    {
        switch (m_status)
        {
            case TxStatus::Pending:
                return "pending";
            case TxStatus::InProgress:
                return m_selfTx  ? "self sending" : (m_sender ? "waiting for receiver" : "waiting for sender");
            case TxStatus::Registering: 
                return m_selfTx ? "self sending" : (m_sender ? "sending" : "receiving" );
            case TxStatus::Failed: 
                return TxFailureReason::TransactionExpired == m_failureReason ? "expired" : "failed";
            case TxStatus::Canceled:
                return "cancelled";
            case TxStatus::Completed:
                return m_selfTx ? "completed" : (m_sender ? "sent" : "received");
            default:
                BOOST_ASSERT_MSG(false, kErrorUnknownTxStatus);
                return "unknown";
        }
    }

    std::string SimpleTxStatusInterpreter::getStatus() const
    {
        switch (m_status)
        {
        case TxStatus::InProgress:
            return m_selfTx ? "sending to own address" : (m_sender ? "waiting for receiver" : "waiting for sender");
        case TxStatus::Registering:
            return m_selfTx ? "sending to own address" : "in progress";
        case TxStatus::Completed:
            return m_selfTx ? "sent to own address" : (m_sender ? "sent" : "received");
        default:
            break;
        }
        return TxStatusInterpreter::getStatus();
    }

    MaxPrivacyTxStatusInterpreter::MaxPrivacyTxStatusInterpreter(const TxParameters& txParams) : TxStatusInterpreter(txParams)
    {
        auto storedType = txParams.GetParameter<TxAddressType>(TxParameterID::AddressType);
        if (storedType)
        {
            m_addressType = *storedType;
        }
        else
        {
            TxDescription tx_desc(txParams);
            m_addressType = tx_desc.m_sender ? GetAddressType(tx_desc) : TxAddressType::Unknown;
        }
    };

    std::string MaxPrivacyTxStatusInterpreter::getStatus() const
    {
        switch (m_addressType)
        {
            case TxAddressType::MaxPrivacy:
                switch (m_status)
                {
                case TxStatus::Registering:
                    return "in progress max privacy";
                case TxStatus::Failed:
                    return TxFailureReason::TransactionExpired == m_failureReason ? "expired" : "failed max privacy";
                case TxStatus::Canceled:
                    return "canceled max privacy";
                case TxStatus::Completed:
                    return m_sender ? "sent max privacy" : "received max privacy";
                default:
                    break;
                }
                break;
            case TxAddressType::Offline:
                switch (m_status)
                {
                case TxStatus::Registering:
                    return "in progress offline";
                case TxStatus::Failed:
                    return TxFailureReason::TransactionExpired == m_failureReason ? "expired" : "failed offline";
                case TxStatus::Canceled:
                    return "canceled offline";
                case TxStatus::Completed:
                    return m_sender ? "sent offline" : "received offline";
                default:
                    break;
                }
                break;
            case TxAddressType::PublicOffline:
                switch (m_status)
                {
                case TxStatus::Registering:
                    return "in progress public offline";
                case TxStatus::Failed:
                    return TxFailureReason::TransactionExpired == m_failureReason ? "expired" : "failed public offline";
                case TxStatus::Canceled:
                    return "canceled public offline";
                case TxStatus::Completed:
                    return m_sender ? "sent public offline" : "received public offline";
                default:
                    break;
                }
                break;
            default:
                break;
        }

        return TxStatusInterpreter::getStatus();
    }

    AssetTxStatusInterpreter::AssetTxStatusInterpreter(const TxParameters& txParams) : TxStatusInterpreter(txParams)
    {
        if (auto value = txParams.GetParameter<TxType>(TxParameterID::TransactionType); value)
            m_txType = *value;
    }

    std::string AssetTxStatusInterpreter::getStatus() const
    {
        if (m_status == TxStatus::InProgress && m_txType == TxType::AssetInfo) return "getting info";
        if (m_status == TxStatus::Completed)
        {
            switch (m_txType)
            {
                case TxType::AssetIssue: return "asset issued";
                case TxType::AssetConsume: return "asset consumed";
                case TxType::AssetReg: return "asset registered";
                case TxType::AssetUnreg: return "asset unregistered";
                case TxType::AssetInfo: return "asset confirmed";
                default: break;
            }
        }

        return TxStatusInterpreter::getStatus();
    }

    ContractTxStatusInterpreter::ContractTxStatusInterpreter(const TxParameters& txParams) : TxStatusInterpreter(txParams)
    {
        if (auto value = txParams.GetParameter<TxType>(TxParameterID::TransactionType); value)
            m_txType = *value;
    }

    std::string ContractTxStatusInterpreter::getStatus() const
    {
        switch (m_status)
        {
        case TxStatus::InProgress:
        case TxStatus::Registering:
            return "in progress";
        case TxStatus::Completed:
            return "completed";
        default:
            break;
        }
        return TxStatusInterpreter::getStatus();
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
            || m_status == TxStatus::Registering;
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
    Amount TxDescription::getExchangeRate(const Currency& targetCurrency) const
    {
        auto exchangeRatesOptional = GetParameter<std::vector<ExchangeRate>>(TxParameterID::ExchangeRates);
        if (exchangeRatesOptional)
        {
            std::vector<ExchangeRate>& rates = *exchangeRatesOptional;

            auto assetIdOptional = GetParameter<Asset::ID>(TxParameterID::AssetID);
            Asset::ID assetId  = assetIdOptional ? *assetIdOptional : 0;
            auto fromCurrency = beam::wallet::Currency(assetId);

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

    std::string TxDescription::getSenderIdentity() const
    {
        return getIdentity(m_sender);
    }

    std::string TxDescription::getReceiverIdentity() const
    {
        return getIdentity(!m_sender);
    }

    std::string TxDescription::getSender() const
    {
        return std::to_string(m_sender ? m_myId : m_peerId);
    }

    std::string TxDescription::getReceiver() const
    {
        return std::to_string(!m_sender ? m_myId : m_peerId);
    }

    std::string TxDescription::getIdentity(bool isSender) const
    {
        auto v = isSender ? GetParameter<PeerID>(TxParameterID::MyWalletIdentity)
            : GetParameter<PeerID>(TxParameterID::PeerWalletIdentity);
        if (v)
        {
            return std::to_string(*v);
        }
        return {};
    }

    std::string TxDescription::getAddressFrom() const
    {
        if (m_txType == wallet::TxType::PushTransaction && !m_sender)
        {
            return getSenderIdentity();
        }
        return std::to_string(m_sender ? m_myId : m_peerId);
    }

    std::string TxDescription::getAddressTo() const
    {
        if (m_sender)
        {
            auto token = getToken();
            if (token.empty())
                return std::to_string(m_peerId);

            return token;
        }
        return std::to_string(m_myId);
    }

    uint64_t get_RandomID()
    {
        uintBigFor<uint64_t>::Type val;
        ECC::GenRandom(val);

        uint64_t ret;
        val.Export(ret);
        return ret;
    }

    std::string GetSendToken(const std::string& sbbsAddress, const std::string& identityStr, Amount amount)
    {
        WalletID walletID;
        if (!walletID.FromHex(sbbsAddress))
        {
            return "";
        }
        auto identity = GetPeerIDFromHex(identityStr);
        if (!identity)
        {
            return "";
        }

        TxParameters parameters;
        if (amount > 0)
        {
            parameters.SetParameter(TxParameterID::Amount, amount);
        }

        parameters.SetParameter(TxParameterID::PeerID, walletID);
        parameters.SetParameter(TxParameterID::TransactionType, beam::wallet::TxType::Simple);
        parameters.SetParameter(TxParameterID::PeerWalletIdentity, *identity);

        return std::to_string(parameters);
    }

    ShieldedVoucherList GenerateVoucherList(const std::shared_ptr<IPrivateKeyKeeper2>& pKeeper, uint64_t ownID, size_t count)
    {
        ShieldedVoucherList res;

        if (pKeeper && count)
        {
            IPrivateKeyKeeper2::Method::CreateVoucherShielded m;
            m.m_MyIDKey = ownID;
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
            params.push_back(
            {
                GetParameterName(pair.first),
                GetParameterValue(pair.first, pair.second)
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

    ShieldedTxo::PublicGen GeneratePublicAddress(Key::IPKdf& kdf, Key::Index index /*= 0*/)
    {
        ShieldedTxo::Viewer viewer;
        viewer.FromOwner(kdf, index);
        ShieldedTxo::PublicGen gen;
        gen.FromViewer(viewer);
        return gen;
    }

    ShieldedTxo::Voucher GenerateVoucherFromPublicAddress(const ShieldedTxo::PublicGen& gen, const Scalar::Native& sk)
    {
        ShieldedTxo::Voucher voucher;
        ECC::Hash::Value nonce;
        ECC::GenRandom(nonce);

        ShieldedTxo::Data::TicketParams tp;
        tp.Generate(voucher.m_Ticket, gen, nonce);

        voucher.m_SharedSecret = tp.m_SharedSecret;

        ECC::Hash::Value hvMsg;
        voucher.get_Hash(hvMsg);
        voucher.m_Signature.Sign(hvMsg, sk);
        return voucher;
    }

    void AppendLibraryVersion(TxParameters& params)
    {
#ifdef BEAM_LIB_VERSION
        params.SetParameter(beam::wallet::TxParameterID::LibraryVersion, std::string(BEAM_LIB_VERSION));
#endif // BEAM_LIB_VERSION
    }

    void ProcessLibraryVersion(const TxParameters& params, VersionFunc&& func)
    {
#ifdef BEAM_LIB_VERSION
        if (auto libVersion = params.GetParameter<std::string>(TxParameterID::LibraryVersion); libVersion)
        {
            std::string myLibVersionStr = BEAM_LIB_VERSION;
            std::regex libVersionRegex("\\d{1,}\\.\\d{1,}\\.\\d{4,}");
            if (std::regex_match(*libVersion, libVersionRegex) &&
                std::lexicographical_compare(
                    myLibVersionStr.begin(),
                    myLibVersionStr.end(),
                    libVersion->begin(),
                    libVersion->end(),
                    std::less<char>{}))
            {
                if (func)
                {
                    func(*libVersion, myLibVersionStr);
                }
                else
                {
                    LOG_WARNING() <<
                        "This address generated by newer Beam library version(" << *libVersion << ")\n" <<
                        "Your version is: " << myLibVersionStr << " Please, check for updates.";
                }
            }
        }
#endif // BEAM_LIB_VERSION
    }

    void ProcessClientVersion(const TxParameters& params, const std::string& appName, const std::string& myClientVersion, VersionFunc&& func)
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
                }
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
