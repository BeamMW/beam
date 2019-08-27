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
            return "btc";
        case beam::wallet::AtomicSwapCoin::Litecoin:
            return "ltc";
        case beam::wallet::AtomicSwapCoin::Qtum:
            return "qtum";
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

    SwapSecondSideChainType SwapSecondSideChainTypeFromString(const std::string& value)
    {
        if (value == "mainnet")
            return SwapSecondSideChainType::Mainnet;
        else if (value == "testnet")
            return SwapSecondSideChainType::Testnet;

        return SwapSecondSideChainType::Unknown;
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

    void PaymentConfirmation::get_Hash(Hash::Value& hv) const
    {
        Hash::Processor()
            << "PaymentConfirmation"
            << m_KernelID
            << m_Sender
            << m_Value
            >> hv;
    }

    bool PaymentConfirmation::IsValid(const PeerID& pid) const
    {
        Point::Native pk;
        if (!proto::ImportPeerID(pk, pid))
            return false;

        Hash::Value hv;
        get_Hash(hv);

        return m_Signature.IsValid(hv, pk);
    }

    void PaymentConfirmation::Sign(const Scalar::Native& sk)
    {
        Hash::Value hv;
        get_Hash(hv);

        m_Signature.Sign(hv, sk);
    }

    std::string TxDescription::getStatusString() const
    {
        switch (m_status)
        {
        case TxStatus::Pending:
            return "pending";
        case TxStatus::InProgress:
            return m_sender == false ? "waiting for sender" : "waiting for receiver";
        case TxStatus::Registering:
            return m_sender == false ? "receiving" : "sending";
        case TxStatus::Completed:
        {
            if (m_selfTx)
            {
                return "completed";
            }
            return m_sender == false ? "received" : "sent";
        }
        case TxStatus::Cancelled:
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
