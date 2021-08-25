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

#include "tx_history_to_csv.h"
#include "core/block_crypt.h"
#include "wallet/core/common.h"
#include <boost/serialization/nvp.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

namespace
{
using namespace beam;
using namespace beam::wallet;
using boost::multiprecision::cpp_dec_float_50;

std::string getIdentity(const TxParameters& txParams, bool isSender)
{
    auto v = isSender ? txParams.GetParameter<PeerID>(TxParameterID::MyWalletIdentity)
                      : txParams.GetParameter<PeerID>(TxParameterID::PeerWalletIdentity);

    return v ? std::to_string(*v) : "";
}

std::string getToken(const TxParameters& txParams)
{
    auto token = txParams.GetParameter<std::string>(TxParameterID::OriginalToken);
    return token ? *token : "";
}

std::string convertAmount(const Amount& amount, const Amount& rate, uint32_t precision)
{
    if (!rate) return "";
    cpp_dec_float_50 dec_first(amount);
    dec_first /= Rules::Coin;
    cpp_dec_float_50 dec_second(rate);
    dec_second /= Rules::Coin;
    cpp_dec_float_50 product = dec_first * dec_second;

    std::ostringstream oss;
    oss.precision(precision);
    oss << std::fixed << product;
    return oss.str();
}
} // namespace

namespace beam::wallet
{
std::string ExportTxHistoryToCsv(const IWalletDB& db)
{
    // TODO:SWAP add to history if necessary https://github.com/BeamMW/beam/issues/1735
    std::stringstream ss;
    ss << "Type" << ","
       << "Date | Time" << ","
       << "\"Amount\"" << ","
       << "\"Unit name\"" << ","
       << "\"Amount, USD\"" << ","
       << "\"Amount, BTC\"" << ","
       << "\"Transaction fee, BEAM\"" << ","
       << "Status" << ","
       << "Comment" << "," 
       << "Transaction ID" << ","
       << "Kernel ID" << "," 
       << "Sending address" << ","
       << "Sending wallet's signature" << ","
       << "Receiving address" << ","
       << "Receiving wallet's signature" << ","
       << "Token" << ","
       << "Payment proof" << std::endl;

    auto transactions = db.getTxHistory(TxType::Simple);
    auto maxPrivacyTx = db.getTxHistory(TxType::PushTransaction);
    transactions.reserve(transactions.size() + maxPrivacyTx.size());
    copy(maxPrivacyTx.begin(), maxPrivacyTx.end(), back_inserter(transactions));
    sort(transactions.begin(), transactions.end(),
        [](const TxDescription& a, const TxDescription& b)
        {
            return a.m_createTime > b.m_createTime;   
        }
    );
    
    for (const auto& tx : transactions)
    {
        std::string strProof;
        if (tx.m_status == TxStatus::Completed &&
            tx.m_sender &&
            !tx.m_selfTx)
        {
            auto proof = storage::ExportPaymentProof(db, tx.m_txId);
            strProof.resize(proof.size() * 2);
            beam::to_hex(strProof.data(), proof.data(), proof.size());
        }

        auto assetIdOptional = tx.GetParameter<Asset::ID>(TxParameterID::AssetID);
        Asset::ID assetId  = assetIdOptional ? *assetIdOptional : 0;
        std::string unitName = "BEAM";
        const auto info = db.findAsset(assetId);
        if (info.is_initialized())
        {
            const WalletAssetMeta &meta = info.is_initialized() ? WalletAssetMeta(*info) : WalletAssetMeta(Asset::Full());
            unitName = meta.GetUnitName();
        }

        std::string amountInUsd = convertAmount(tx.m_amount, tx.getExchangeRate(Currency::USD()), 2);
        std::string amountInBtc = convertAmount(tx.m_amount, tx.getExchangeRate(Currency::BTC()), 8);

        auto statusInterpreter = db.getStatusInterpreter(tx);
        ss << (tx.m_sender ? "Send" : "Receive") << ","                                     // Type
            << format_timestamp(kTimeStampFormatCsv, tx.m_createTime * 1000, false) << ","   // Date | Time
            << "\"" << PrintableAmount(tx.m_amount, true) << "\"" << ","                     // Amount
            << "\"" << unitName << "\"" << ","                                               // Unit name
            << "\"" << amountInUsd << "\"" << ","                                            // Amount, USD
            << "\"" << amountInBtc << "\"" << ","                                            // Amount, BTC
            << "\"" << PrintableAmount(tx.m_fee, true) << "\"" << ","                        // Transaction fee, BEAM
            << statusInterpreter->getStatus() << ","                                         // Status
            << std::string { tx.m_message.begin(), tx.m_message.end() } << ","               // Comment
            << to_hex(tx.m_txId.data(), tx.m_txId.size()) << ","                             // Transaction ID
            << std::to_string(tx.m_kernelID) << ","                                          // Kernel ID
            << std::to_string(tx.m_sender ? tx.m_myId : tx.m_peerId) << ","                  // Sending address
            << getIdentity(tx, tx.m_sender) << ","                                           // Sending wallet's signature
            << std::to_string(!tx.m_sender ? tx.m_myId : tx.m_peerId) << ","                 // Receiving address
            << getIdentity(tx, !tx.m_sender) << ","                                          // Receiving wallet's signature
            << getToken(tx) << ","                                                           // Token
            << strProof << std::endl;                                                        // Payment proof
    }
    return ss.str();
}
} // namespace beam::wallet
