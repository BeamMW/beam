// Copyright 2019 The Beam Team
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
#include "wallet/base58.h"

#include <iomanip>
#include <boost/algorithm/string.hpp>

using namespace std;
using namespace ECC;
using namespace beam;

namespace std
{
    string to_string(const beam::wallet::WalletID& id)
    {
        static_assert(sizeof(id) == sizeof(id.m_Channel) + sizeof(id.m_Pk), "");

        char szBuf[sizeof(id) * 2 + 1];
        beam::to_hex(szBuf, &id, sizeof(id));

        const char* szPtr = szBuf;
        while (*szPtr == '0')
            szPtr++;

        if (!*szPtr)
            szPtr--; // leave at least 1 symbol

        return szPtr;
    }

    string to_string(const Merkle::Hash& hash)
    {
        char sz[Merkle::Hash::nTxtLen + 1];
        hash.Print(sz);
        return string(sz);
    }

    string to_string(beam::wallet::AtomicSwapCoin value)
    {
        switch (value)
        {
        case beam::wallet::AtomicSwapCoin::Bitcoin:
            return "BTC";
        case beam::wallet::AtomicSwapCoin::Litecoin:
            return "LTC";
        case beam::wallet::AtomicSwapCoin::Qtum:
            return "QTUM";
        default:
            return "";
        }
    }

    string to_string(beam::wallet::SwapOfferStatus status)
    {
        switch (status)
        {
        case beam::wallet::SwapOfferStatus::Pending:
            return "Pending";
        case beam::wallet::SwapOfferStatus::InProgress:
            return "InProgress";
        case beam::wallet::SwapOfferStatus::Completed:
            return "Completed";
        case beam::wallet::SwapOfferStatus::Canceled:
            return "Canceled";
        case beam::wallet::SwapOfferStatus::Expired:
            return "Expired";
        case beam::wallet::SwapOfferStatus::Failed:
            return "Failed";

        default:
            return "";
        }
    }

    string to_string(const beam::wallet::PrintableAmount& amount)
    {
        stringstream ss;

        if (amount.m_showPoint)
        {
            size_t maxGrothsLength = std::lround(std::log10(Rules::Coin));
            ss << fixed << setprecision(maxGrothsLength) << double(amount.m_value) / Rules::Coin;
            string s = ss.str();
            boost::algorithm::trim_right_if(s, boost::is_any_of("0"));
            boost::algorithm::trim_right_if(s, boost::is_any_of(",."));
            return s;
        }
        else
        {
            if (amount.m_value >= Rules::Coin)
            {
                ss << Amount(amount.m_value / Rules::Coin) << " beams ";
            }
            Amount c = amount.m_value % Rules::Coin;
            if (c > 0 || amount.m_value == 0)
            {
                ss << c << " groth ";
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
}

namespace beam
{
    std::ostream& operator<<(std::ostream& os, const wallet::TxID& uuid)
    {
        stringstream ss;
        ss << "[" << to_hex(uuid.data(), uuid.size()) << "]";
        os << ss.str();
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const wallet::PrintableAmount& amount)
    {
        os << std::to_string(amount);
        
        return os;
    }
}

namespace beam::wallet
{
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
        Point::Native p;
        return proto::ImportPeerID(p, m_Pk);
    }

    AtomicSwapCoin from_string(const std::string& value)
    {
        if (value == "btc")
            return AtomicSwapCoin::Bitcoin;
        else if (value == "ltc")
            return AtomicSwapCoin::Litecoin;
        else if (value == "qtum")
            return AtomicSwapCoin::Qtum;

        return AtomicSwapCoin::Unknown;
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

    Amount GetMinimumFee(size_t numberOfOutputs, size_t numberOfKenrnels /*= 1*/)
    {
        // Minimum Fee = (number of outputs) * 10 + (number of kernels) * 10
        return (numberOfOutputs + numberOfKenrnels) * 10;
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
        if (!proto::ImportPeerID(pk, pid))
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
        Hash::Processor()
            << "PaymentConfirmation"
            << m_KernelID
            << m_Sender
            << m_Value
            >> hv;
    }

    void SwapOfferConfirmation::get_Hash(Hash::Value& hv) const
    {
        beam::Blob data(m_offerData);
        Hash::Processor()
            << "SwapOfferSignature"
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

    boost::optional<TxID> TxParameters::GetTxID() const
    {
        return m_ID;
    }

    boost::optional<ByteBuffer> TxParameters::GetParameter(TxParameterID parameterID, SubTxID subTxID) const
    {
        auto subTxIt = m_Parameters.find(subTxID);
        if (subTxIt == m_Parameters.end())
        {
            return {};
        }
        auto it = subTxIt->second.find(parameterID);
        if (it == subTxIt->second.end())
        {
            return {};
        }
        return boost::optional<ByteBuffer>(it->second);
    }

    TxParameters& TxParameters::SetParameter(TxParameterID parameterID, const ByteBuffer& parameter, SubTxID subTxID)
    {
        m_Parameters[subTxID][parameterID] = parameter;
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

                return boost::make_optional<TxParameters>(token.UnpackParameters());
            }
            catch (...)
            {
                // failed to deserialize
            }
        }
        else // plain WalletID
        {
            WalletID walletID;
            if (walletID.FromBuf(buffer))
            {
                auto result = boost::make_optional<TxParameters>({});
                result->SetParameter(TxParameterID::PeerID, walletID);
                return result;
            }
        }
        return {};
    }

    void SwapOffer::SetTxParameters(const PackedTxParameters& parameters)
    {
        // Do not forget to set other SwapOffer members also!
        SubTxID subTxID = kDefaultSubTxID;
        Deserializer d;
        for (const auto& p : parameters)
        {
            if (p.first == TxParameterID::SubTxIndex)
            {
                // change subTxID
                d.reset(p.second.data(), p.second.size());
                d & subTxID;
                continue;
            }

            SetParameter(p.first, p.second, subTxID);
        }
    }

    SwapOffer SwapOfferToken::Unpack() const
    {
        SwapOffer result(m_TxID);
        result.SetTxParameters(m_Parameters);

        if (m_TxID) result.m_txId = *m_TxID;
        if (m_status) result.m_status = *m_status;
        if (m_publisherId) result.m_publisherId = *m_publisherId;
        if (m_coin) result.m_coin = *m_coin;
        return result;
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

    std::string TxDescription::getStatusString() const
    {
        switch (m_status)
        {
        case TxStatus::Pending:
            return "pending";
        case TxStatus::InProgress:
        {
            if (m_selfTx)
            {
                return "self sending";
            }
            return m_sender == false ? "waiting for sender" : "waiting for receiver";
        }
        case TxStatus::Registering:
        {
            if (m_selfTx)
            {
                return "self sending";
            }
            return m_sender == false ? "receiving" : "sending";
        }
        case TxStatus::Completed:
        {
            if (m_selfTx)
            {
                return "completed";
            }
            return m_sender == false ? "received" : "sent";
        }
        case TxStatus::Canceled:
            return "cancelled";
        case TxStatus::Failed:
            if (TxFailureReason::TransactionExpired == m_failureReason)
            {
                return "expired";
            }
            return "failed";
        default:
            break;
        }

        assert(false && "Unknown TX status!");
        return "unknown";
    }
}
