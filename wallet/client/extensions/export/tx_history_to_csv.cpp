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
#include "bvm/invoke_data.h"
#include "core/block_crypt.h"
#include "wallet/core/common.h"
#include "wallet/transactions/swaps/swap_tx_description.h"
#include <boost/serialization/nvp.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

namespace
{
using namespace beam;
using namespace beam::wallet;
using boost::multiprecision::cpp_dec_float_50;

std::string unitNameFromAssetId(const beam::wallet::IWalletDB& db, Asset::ID assetId)
{
    std::string unitName = "BEAM";
    const auto info = db.findAsset(assetId);
    if (info.is_initialized())
    {
        const WalletAssetMeta &meta = info.is_initialized() ? WalletAssetMeta(*info) : WalletAssetMeta(Asset::Full());
        unitName = meta.GetUnitName();
    }

    return unitName;
}

std::string getEndpoint(const TxParameters& txParams, bool isSender)
{
    auto v = isSender ? txParams.GetParameter<PeerID>(TxParameterID::MyEndpoint)
                      : txParams.GetParameter<PeerID>(TxParameterID::PeerEndpoint);

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
    std::stringstream ss;
    ss << "Type" << ","
       << "Date | Time" << ","
       << "Amount" << ","
       << "Unit name" << ","
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
        std::string unitName = unitNameFromAssetId(db, assetId);


        std::string amountInUsd = convertAmount(tx.m_amount, tx.getExchangeRate(Currency::USD()), 2);
        std::string amountInBtc = convertAmount(tx.m_amount, tx.getExchangeRate(Currency::BTC()), 8);

        ss << (tx.m_sender ? "Send" : "Receive") << ","                                     // Type
           << format_timestamp(kTimeStampFormatCsv, tx.m_createTime * 1000, false) << ","   // Date | Time
           << "\"" << PrintableAmount(tx.m_amount, true) << "\"" << ","                     // Amount
           << "\"" << unitName << "\"" << ","                                               // Unit name
           << "\"" << amountInUsd << "\"" << ","                                            // Amount, USD
           << "\"" << amountInBtc << "\"" << ","                                            // Amount, BTC
           << "\"" << PrintableAmount(tx.m_fee, true) << "\"" << ","                        // Transaction fee, BEAM
           << beam::wallet::interpretStatus(tx) << ","                                      // Status
           << std::string { tx.m_message.begin(), tx.m_message.end() } << ","               // Comment
           << to_hex(tx.m_txId.data(), tx.m_txId.size()) << ","                             // Transaction ID
           << std::to_string(tx.m_kernelID) << ","                                          // Kernel ID
           << std::to_string(tx.m_sender ? tx.m_myAddr : tx.m_peerAddr) << ","              // Sending address
           << getEndpoint(tx, tx.m_sender) << ","                                           // Sending wallet's endpoint
           << std::to_string(!tx.m_sender ? tx.m_myAddr : tx.m_peerAddr) << ","             // Receiving address
           << getEndpoint(tx, !tx.m_sender) << ","                                          // Receiving wallet's endpoint
           << getToken(tx) << ","                                                           // Token
           << strProof << std::endl;                                                        // Payment proof
    }
    return ss.str();
}

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
std::string ExportAtomicSwapTxHistoryToCsv(const IWalletDB& db)
{
    std::stringstream ss;
    ss << "Date | Time" << ","
       << "Amount sent" << ","
       << "Unit name sent" << ","
       << "Amount received" << ","
       << "Unit name received" << ","
       << "\"Transaction fee, BEAM\"" << ","
       << "\"Swap coin fee rate\"" << ","
       << "Status" << ","
       << "Peer address" << ","
       << "My address" << ","
       << "Transaction ID" << std::endl;

    auto atomicSwapTransactions = db.getTxHistory(TxType::AtomicSwap);
    for (const auto& tx : atomicSwapTransactions)
    {
        auto stx = beam::wallet::SwapTxDescription(tx);
        bool isBeamSide = stx.isBeamSide();
        Amount swapAmount = stx.getSwapAmount();
        std::string swapCoin = std::to_string(stx.getSwapCoin());
        Amount beamAmount = tx.m_amount;
        std::string beamUnit = "BEAM";

        ss  << format_timestamp(kTimeStampFormatCsv, tx.m_createTime * 1000, false) << "," // Date | Time
            << PrintableAmount(isBeamSide ? beamAmount : swapAmount, true) << ","          // "Amount sent"
            << (isBeamSide ? beamUnit : swapCoin) << ","                                   // "Unit name sent"
            << PrintableAmount(isBeamSide ? swapAmount : beamAmount, true) << ","          // "Amount received"
            << (isBeamSide ? swapCoin : beamUnit)  << ","                                  // "Unit name received"
            << "\"" << PrintableAmount(*(stx.getFee()), true) << "\"" << ","               // Transaction fee, BEAM
            << "\"" << PrintableAmount(*(stx.getSwapCoinFeeRate()), true) << "\"" << ","   // Swap coin fee rate
            << beam::wallet::GetSwapTxStatusStr(tx) << ","                                 // Status
            << std::to_string(tx.m_peerAddr) << ","                                        // Peer address
            << std::to_string(tx.m_myAddr) << ","                                          // My address
            << to_hex(tx.m_txId.data(), tx.m_txId.size()) << std::endl;                    // Transaction ID
    }

    return ss.str();
}
#endif // BEAM_ATOMIC_SWAP_SUPPORT

#ifdef BEAM_ASSET_SWAP_SUPPORT
std::string ExportAssetsSwapTxHistoryToCsv(const IWalletDB& db)
{
    std::stringstream ss;
    ss << "Date | Time" << ","
       << "Amount sent" << ","
       << "Unit name sent" << ","
       << "Amount received" << ","
       << "Unit name received" << ","
       << "\"Transaction fee, BEAM\"" << ","
       << "Status" << ","
       << "Comment" << "," 
       << "Transaction ID" << ","
       << "Kernel ID" << "," 
       << "Peer address" << ","
       << "My address" << std::endl;

    auto assetsSwapTransactions = db.getTxHistory(TxType::DexSimpleSwap);
    for (const auto& tx : assetsSwapTransactions)
    {
        const auto rasset  = tx.GetParameter<beam::Asset::ID>(TxParameterID::DexReceiveAsset);
        const auto ramount = tx.GetParameter<beam::Amount>(TxParameterID::DexReceiveAmount);

        ss  << format_timestamp(kTimeStampFormatCsv, tx.m_createTime * 1000, false) << ","     // Date | Time
            << PrintableAmount(tx.m_amount, true) << ","                                       // Sent
            << unitNameFromAssetId(db, tx.m_assetId) << ","                                    // Unit name sent
            << PrintableAmount(*ramount, true) << ","                                          // Receive
            << unitNameFromAssetId(db, *rasset) << ","                                         // Unit name received
            << "\"" << PrintableAmount(tx.m_fee, true) << "\"" << ","                          // Transaction fee, BEAM
            << beam::wallet::interpretStatus(tx) << ","                                        // Status
            << "\"" << std::string { tx.m_message.begin(), tx.m_message.end() } << "\"" << "," // Comment
            << to_hex(tx.m_txId.data(), tx.m_txId.size()) << ","                               // Transaction ID
            << std::to_string(tx.m_kernelID) << ","                                            // Kernel ID
            << std::to_string(tx.m_peerAddr) << ","                                            // Peer address
            << std::to_string(tx.m_myAddr) << std::endl;                                       // My address
    }

    return ss.str();
}
#endif  // BEAM_ASSET_SWAP_SUPPORT

std::string ExportContractTxHistoryToCsv(const IWalletDB& db)
{
    std::stringstream ss;
    ss << "Date | Time" << ","
       << "Send" << ","
       << "Receive" << ","
       << "\"Transaction fee, BEAM\"" << ","
       << "Status" << ","
       << "DApp name" << "," 
       << "Application shader ID" << ","
       << "Description" << "," 
       << "Transaction ID" << ","
       << "Kernel ID" << std::endl;

    auto contractTransactions = db.getTxHistory(TxType::Contract);
    for (const auto& tx : contractTransactions)
    {
        beam::bvm2::ContractInvokeData vData;

        beam::Amount contractFee = 0;
        beam::bvm2::FundsMap contractSpend;
        std::string contractCids;
        if (tx.GetParameter(TxParameterID::ContractDataPacked, vData))
        {
            Height h = tx.m_minHeight;
            contractFee = vData.get_FullFee(h);
            contractSpend = vData.get_FullSpend();
        }

        if (!vData.m_vec.empty())
        {
            std::stringstream ss2;
            ss2 << vData.m_vec[0].m_Cid.str();

            if (vData.m_vec.size() > 1)
            {
                ss2 << " +" << vData.m_vec.size() - 1;
            }

            contractCids = ss2.str();
        }

        std::stringstream sent;
        std::stringstream received;
        for (const auto& info: contractSpend)
        {
            auto amount = info.second;
            if (info.first == beam::Asset::s_BeamID)
            {
                if (amount < 0)
                {
                    amount += contractFee;
                }
            }

            if (amount <= 0)
            {
                if (!received.str().empty())
                {
                    received << " | " ;
                }
                received << PrintableAmount(static_cast<Amount>(std::abs(amount)), true)
                         << " " << unitNameFromAssetId(db, info.first);
            }
            else
            {
                if (!sent.str().empty())
                {
                    sent << " | ";
                }
                sent << PrintableAmount(static_cast<Amount>(amount), true) 
                     << " " << unitNameFromAssetId(db, info.first);
            }
        }

        ss  << format_timestamp(kTimeStampFormatCsv, tx.m_createTime * 1000, false) << ","     // Date | Time
            << "\"" << (sent.str().empty() ? "-" : sent.str()) << "\"" << ","                  // Send
            << "\"" << (received.str().empty() ? "-" : received.str()) << "\"" << ","          // Receive
            << "\"" << PrintableAmount(contractFee, true) << "\"" << ","                       // Transaction fee, BEAM
            << beam::wallet::interpretStatus(tx) << ","                                        // Status
            << "\"" << tx.m_appName << "\"" << ","                                             // DApp name
            << contractCids << ","                                                             // Application shader ID
            << "\"" << std::string { tx.m_message.begin(), tx.m_message.end() } << "\"" << "," // Description
            << to_hex(tx.m_txId.data(), tx.m_txId.size()) << ","                               // Transaction ID
            << std::to_string(tx.m_kernelID) << std::endl;                                     // Kernel ID
    }

    return ss.str();
}
} // namespace beam::wallet
