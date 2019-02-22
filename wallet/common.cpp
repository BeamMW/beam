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

using namespace std;
using namespace ECC;
using namespace beam;

namespace std
{
    string to_string(const beam::WalletID& id)
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
}

namespace beam::wallet
{
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
}
