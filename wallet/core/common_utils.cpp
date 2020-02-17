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

#include "wallet/core/common_utils.h"

#include "wallet/core/base_transaction.h"
#include "wallet/core/strings_resources.h"
#include "utility/logger.h"

#include <boost/format.hpp>

namespace beam::wallet
{
WalletAddress GenerateNewAddress(
        const IWalletDB::Ptr& walletDB,
        const std::string& label,
        WalletAddress::ExpirationStatus expirationStatus,
        bool saveRequired)
{
    WalletAddress address;
    walletDB->createAddress(address);

    address.setExpiration(expirationStatus);
    address.m_label = label;
    if (saveRequired)
    {
        walletDB->saveAddress(address);
    }

    LOG_INFO() << boost::format(kAddrNewGenerated) 
                % std::to_string(address.m_walletID);
    if (!label.empty()) {
        LOG_INFO() << boost::format(kAddrNewGeneratedLabel) % label;
    }
    return address;
}

bool ReadTreasury(ByteBuffer& bb, const std::string& sPath)
{
    if (sPath.empty())
        return false;

    std::FStream f;
    if (!f.Open(sPath.c_str(), true))
        return false;

    size_t nSize = static_cast<size_t>(f.get_Remaining());
    if (!nSize)
        return false;

    bb.resize(f.get_Remaining());
    return f.read(&bb.front(), nSize) == nSize;
}

std::string TxIDToString(const TxID& txId)
{
    return to_hex(txId.data(), txId.size());
}

void GetStatusResponseJson(const TxDescription& tx,
                           nlohmann::json& msg,
                           Height kernelProofHeight,
                           Height systemHeight)
{
    msg = nlohmann::json
    {
        {"txId", TxIDToString(tx.m_txId)},
        {"status", tx.m_status},
        {"status_string", tx.getStatusStringApi()},
        {"sender", std::to_string(tx.m_sender ? tx.m_myId : tx.m_peerId)},
        {"receiver", std::to_string(tx.m_sender ? tx.m_peerId : tx.m_myId)},
        {"fee", tx.m_fee},
        {"value", tx.m_amount},
        {"comment", std::string{ tx.m_message.begin(), tx.m_message.end() }},
        {"create_time", tx.m_createTime},            
        {"income", !tx.m_sender}
    };

    if (kernelProofHeight > 0)
    {
        msg["height"] = kernelProofHeight;

        if (systemHeight >= kernelProofHeight)
        {
            msg["confirmations"] = systemHeight - kernelProofHeight;
        }
    }

    if (tx.m_status == TxStatus::Failed)
    {
        msg["failure_reason"] = GetFailureMessage(tx.m_failureReason);
    }
    else if (tx.m_status != TxStatus::Canceled)
    {
        msg["kernel"] = to_hex(tx.m_kernelID.m_pData, tx.m_kernelID.nBytes);
    }
}

}  // namespace beam::wallet
