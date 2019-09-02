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

#include "wallet/wallet_network.h"
#include "core/common.h"

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"
#include "wallet/secstring.h"
#include "wallet/qtum/options.h"
#include "wallet/litecoin/options.h"
#include "wallet/bitcoin/options.h"
#include "wallet/bitcoin/bitcoin_side.h"
#include "wallet/litecoin/litecoin_side.h"
#include "wallet/qtum/qtum_side.h"
#include "wallet/strings_resources.h"
#include "wallet/swaps/common.h"
#include "wallet/swaps/swap_transaction.h"
#include "wallet/local_private_key_keeper.h"
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "core/treasury.h"
#include "core/block_rw.h"
#include "unittests/util.h"
#include "mnemonic/mnemonic.h"
#include "utility/string_helpers.h"
#include "version.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include "utility/cli/options.h"
#include "utility/log_rotation.h"
#include "utility/helpers.h"

#include <boost/assert.hpp> 
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <iomanip>
#include <iterator>
#include <future>

using namespace std;
using namespace beam;
using namespace beam::wallet;
using namespace ECC;

namespace beam
{
    string getCoinStatus(Coin::Status s)
    {
        stringstream ss;
        ss << "[";
        switch (s)
        {
        case Coin::Available: ss << kCoinStatusAvailable; break;
        case Coin::Unavailable: ss << kCoinStatusUnavailable; break;
        case Coin::Spent: ss << kCoinStatusSpent; break;
        case Coin::Maturing: ss << kCoinStatusMaturing; break;
        case Coin::Outgoing: ss << kCoinStatusOutgoing; break;
        case Coin::Incoming: ss << kCoinStatusIncoming; break;
        default:
            BOOST_ASSERT_MSG(false, kErrorUnknownCoinStatus);
        }
        ss << "]";
        string str = ss.str();
        BOOST_ASSERT(str.length() <= 30);
        return str;
    }

    const char* getTxStatus(const TxDescription& tx)
    {
        switch (tx.m_status)
        {
        case TxStatus::Pending: return kTxStatusPending;
        case TxStatus::InProgress: return tx.m_sender ? kTxStatusWaitingForReceiver : kTxStatusWaitingForSender;
        case TxStatus::Registering: return tx.m_sender ? kTxStatusSending : kTxStatusReceiving;
        case TxStatus::Cancelled: return kTxStatusCancelled;
        case TxStatus::Completed:
        {
            if (tx.m_selfTx)
            {
                return kTxStatusCompleted;
            }
            return tx.m_sender ? kTxStatusSent : kTxStatusReceived;
        }
        case TxStatus::Failed: return TxFailureReason::TransactionExpired == tx.m_failureReason
            ? kTxStatusExpired : kTxStatusFailed;
        default:
            BOOST_ASSERT_MSG(false, kErrorUnknowmTxStatus);
        }

        return "";
    }

    const char* getSwapTxStatus(const IWalletDB::Ptr& walletDB, const TxDescription& tx)
    {
        wallet::AtomicSwapTransaction::State state = wallet::AtomicSwapTransaction::State::CompleteSwap;
        storage::getTxParameter(*walletDB, tx.m_txId, wallet::TxParameterID::State, state);

        switch (state)
        {
        case wallet::AtomicSwapTransaction::State::Initial:
            return kSwapTxStatusInitial;
        case wallet::AtomicSwapTransaction::State::Invitation:
            return kSwapTxStatusInvitation;
        case wallet::AtomicSwapTransaction::State::BuildingBeamLockTX:
            return kSwapTxStatusBuildingBeamLockTX;
        case wallet::AtomicSwapTransaction::State::BuildingBeamRefundTX:
            return kSwapTxStatusBuildingBeamRefundTX;
        case wallet::AtomicSwapTransaction::State::BuildingBeamRedeemTX:
            return kSwapTxStatusBuildingBeamRedeemTX;
        case wallet::AtomicSwapTransaction::State::HandlingContractTX:
            return kSwapTxStatusHandlingContractTX;
        case wallet::AtomicSwapTransaction::State::SendingRefundTX:
            return kSwapTxStatusSendingRefundTX;
        case wallet::AtomicSwapTransaction::State::SendingRedeemTX:
            return kSwapTxStatusSendingRedeemTX;
        case wallet::AtomicSwapTransaction::State::SendingBeamLockTX:
            return kSwapTxStatusSendingBeamLockTX;
        case wallet::AtomicSwapTransaction::State::SendingBeamRefundTX:
            return kSwapTxStatusSendingBeamRefundTX;
        case wallet::AtomicSwapTransaction::State::SendingBeamRedeemTX:
            return kSwapTxStatusSendingBeamRedeemTX;
        case wallet::AtomicSwapTransaction::State::CompleteSwap:
            return kSwapTxStatusCompleted;
        case wallet::AtomicSwapTransaction::State::Cancelled:
            return kSwapTxStatusCancelled;
        case wallet::AtomicSwapTransaction::State::Refunded:
            return kSwapTxStatusAborted;
        case wallet::AtomicSwapTransaction::State::Failed:
        {
            TxFailureReason reason = TxFailureReason::Unknown;
            storage::getTxParameter(*walletDB, tx.m_txId, wallet::TxParameterID::InternalFailureReason, reason);

            return TxFailureReason::TransactionExpired == reason ? kSwapTxStatusExpired : kSwapTxStatusFailed;
        }
        default:
            BOOST_ASSERT_MSG(false, kErrorUnknowmTxStatus);
        }

        return "";
    }

    const char* getAtomicSwapCoinText(AtomicSwapCoin swapCoin)
    {
        switch (swapCoin)
        {
        case AtomicSwapCoin::Bitcoin:
            return kSwapCoinBTC;
        case AtomicSwapCoin::Litecoin:
            return kSwapCoinLTC;
        case AtomicSwapCoin::Qtum:
            return kSwapCoinQTUM;
        default:
            BOOST_ASSERT_MSG(false, kErrorUnknownSwapCoin);
        }
        return "";
    }
}
namespace
{
    void ResolveWID(PeerID& res, const std::string& s)
    {
        bool bValid = true;
        ByteBuffer bb = from_hex(s, &bValid);

        if ((bb.size() != res.nBytes) || !bValid)
            throw std::runtime_error(kErrorInvalidWID);

        memcpy(res.m_pData, &bb.front(), res.nBytes);
    }

    template <typename T>
    bool FLoad(T& x, const std::string& sPath, bool bStrict = true)
    {
        std::FStream f;
        if (!f.Open(sPath.c_str(), true, bStrict))
            return false;

        yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(f);
        arc & x;
        return true;
    }

    template <typename T>
    void FSave(const T& x, const std::string& sPath)
    {
        std::FStream f;
        f.Open(sPath.c_str(), false, true);

        yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> arc(f);
        arc & x;
    }

    int HandleTreasury(const po::variables_map& vm, Key::IKdf& kdf)
    {
        PeerID wid;
        Scalar::Native sk;
        Treasury::get_ID(kdf, wid, sk);

        char szID[PeerID::nTxtLen + 1];
        wid.Print(szID);

        static const char* szPlans = "treasury_plans.bin";
        static const char* szRequest = "-plan.bin";
        static const char* szResponse = "-response.bin";
        static const char* szData = "treasury_data.bin";

        Treasury tres;
        FLoad(tres, szPlans, false);


        auto nCode = vm[cli::TR_OPCODE].as<uint32_t>();
        switch (nCode)
        {
        default:
            cout << boost::format(kTreasuryID) % szID << std::endl;
            break;

        case 1:
        {
            // generate plan
            std::string sID = vm[cli::TR_WID].as<std::string>();
            ResolveWID(wid, sID);

            auto perc = vm[cli::TR_PERC].as<double>();

            bool bConsumeRemaining = (perc <= 0.);
            if (bConsumeRemaining)
                perc = vm[cli::TR_PERC_TOTAL].as<double>();

            perc *= 0.01;

            Amount val = static_cast<Amount>(Rules::get().Emission.Value0 * perc); // rounded down

            Treasury::Parameters pars; // default

            uint32_t m = vm[cli::TR_M].as<uint32_t>();
            uint32_t n = vm[cli::TR_N].as<uint32_t>();

            if (m >= n)
                throw std::runtime_error(kErrorTreasuryBadM);

            BOOST_ASSERT(n);
            if (pars.m_Bursts % n)
                throw std::runtime_error(kErrorTreasuryBadN);

            pars.m_Bursts /= n;
            pars.m_Maturity0 = pars.m_MaturityStep * pars.m_Bursts * m;

            Treasury::Entry* pE = tres.CreatePlan(wid, val, pars);

            if (bConsumeRemaining)
            {
                // special case - consume the remaining
                for (size_t iG = 0; iG < pE->m_Request.m_vGroups.size(); iG++)
                {
                    Treasury::Request::Group& g = pE->m_Request.m_vGroups[iG];
                    Treasury::Request::Group::Coin& c = g.m_vCoins[0];

                    AmountBig::Type valInBurst = Zero;

                    for (Treasury::EntryMap::const_iterator it = tres.m_Entries.begin(); tres.m_Entries.end() != it; it++)
                    {
                        if (&it->second == pE)
                            continue;

                        const Treasury::Request& r2 = it->second.m_Request;
                        for (size_t iG2 = 0; iG2 < r2.m_vGroups.size(); iG2++)
                        {
                            const Treasury::Request::Group& g2 = r2.m_vGroups[iG2];
                            if (g2.m_vCoins[0].m_Incubation != c.m_Incubation)
                                continue;

                            for (size_t i = 0; i < g2.m_vCoins.size(); i++)
                                valInBurst += uintBigFrom(g2.m_vCoins[i].m_Value);
                        }
                    }

                    Amount vL = AmountBig::get_Lo(valInBurst);
                    if (AmountBig::get_Hi(valInBurst) || (vL >= c.m_Value))
                        throw std::runtime_error(kErrorTreasuryNothingRemains);

                    cout << boost::format(kTreasuryConsumeRemaining) % c.m_Incubation % vL % c.m_Value << std::endl;
                    c.m_Value -= vL;
                }

            }

            FSave(pE->m_Request, sID + szRequest);
            FSave(tres, szPlans);
        }
        break;

        case 2:
        {
            // generate response
            Treasury::Request treq;
            FLoad(treq, std::string(szID) + szRequest);

            Treasury::Response tresp;
            uint64_t nIndex = 1;
            tresp.Create(treq, kdf, nIndex);

            FSave(tresp, std::string(szID) + szResponse);
        }
        break;

        case 3:
        {
            // verify & import reponse
            std::string sID = vm[cli::TR_WID].as<std::string>();
            ResolveWID(wid, sID);

            Treasury::EntryMap::iterator it = tres.m_Entries.find(wid);
            if (tres.m_Entries.end() == it)
                throw std::runtime_error(kErrorTreasuryPlanNotFound);

            Treasury::Entry& e = it->second;
            e.m_pResponse.reset(new Treasury::Response);
            FLoad(*e.m_pResponse, sID + szResponse);

            if (!e.m_pResponse->IsValid(e.m_Request))
                throw std::runtime_error(kErrorTreasuryInvalidResponse);

            FSave(tres, szPlans);
        }
        break;

        case 4:
        {
            // Finally generate treasury
            Treasury::Data data;
            data.m_sCustomMsg = vm[cli::TR_COMMENT].as<std::string>();
            tres.Build(data);

            FSave(data, szData);

            Serializer ser;
            ser & data;

            ByteBuffer bb;
            ser.swap_buf(bb);

            Hash::Value hv;
            Hash::Processor() << Blob(bb) >> hv;

            char szHash[Hash::Value::nTxtLen + 1];
            hv.Print(szHash);

            cout << boost::format(kTreasuryDataHash) % szHash << std::endl;

        }
        break;

        case 5:
        {
            // recover and print
            Treasury::Data data;
            FLoad(data, szData);

            std::vector<Treasury::Data::Coin> vCoins;
            data.Recover(kdf, vCoins);

            cout << boost::format(kTreasuryRecoveredCoinsTitle) % vCoins.size() << std::endl;

            for (size_t i = 0; i < vCoins.size(); i++)
            {
                const Treasury::Data::Coin& coin = vCoins[i];
                cout << boost::format(kTreasuryRecoveredCoin) % coin.m_Kidv % coin.m_Incubation << std::endl;

            }
        }
        break;

        case 6:
        {
            // bursts
            Treasury::Data data;
            FLoad(data, szData);

            auto vBursts = data.get_Bursts();

            cout << boost::format(kTreasuryBurstsTitle) % vBursts.size() << std::endl;

            for (size_t i = 0; i < vBursts.size(); i++)
            {
                const Treasury::Data::Burst& b = vBursts[i];
                cout << boost::format(kTreasuryBurst) % b.m_Height % b.m_Value << std::endl;
            }
        }
        break;
        }

        return 0;
    }

    void printHelp(const po::options_description& options)
    {
        cout << options << std::endl;
    }

    int ChangeAddressExpiration(const po::variables_map& vm, const IWalletDB::Ptr& walletDB)
    {
        string address = vm[cli::WALLET_ADDR].as<string>();
        string expiration = vm[cli::EXPIRATION_TIME].as<string>();
        WalletID walletID(Zero);
        bool allAddresses = address == "*";

        if (!allAddresses)
        {
            walletID.FromHex(address);
        }

        WalletAddress::ExpirationStatus expirationStatus;
        if (expiration == cli::EXPIRATION_TIME_24H)
        {
            expirationStatus = WalletAddress::ExpirationStatus::OneDay;
        }
        else if (expiration == cli::EXPIRATION_TIME_NEVER)
        {
            expirationStatus = WalletAddress::ExpirationStatus::Never;
        }
        else if (expiration == cli::EXPIRATION_TIME_NOW)
        {
            expirationStatus = WalletAddress::ExpirationStatus::Expired;
        }
        else
        {
            LOG_ERROR() << boost::format(kErrorAddrExprTimeInvalid)
                        % cli::EXPIRATION_TIME
                        % expiration;
            return -1;
        }

        if (storage::changeAddressExpiration(*walletDB, walletID, expirationStatus))
        {
            if (allAddresses)
            {
                LOG_INFO() << boost::format(kAllAddrExprChanged) % expiration;
            }
            else
            {
                LOG_INFO() << boost::format(kAddrExprChanged) % to_string(walletID) % expiration;
            }
            return 0;
        }
        return -1;
    }

    WalletAddress GenerateNewAddress(
        const IWalletDB::Ptr& walletDB,
        const std::string& label,
        IPrivateKeyKeeper::Ptr keyKeeper,
        WalletAddress::ExpirationStatus expirationStatus = WalletAddress::ExpirationStatus::OneDay)
    {
        WalletAddress address = storage::createAddress(*walletDB, keyKeeper);

        address.setExpiration(expirationStatus);
        address.m_label = label;
        walletDB->saveAddress(address);

        LOG_INFO() << boost::format(kAddrNewGenerated) % std::to_string(address.m_walletID);
        if (!label.empty()) {
            LOG_INFO() << boost::format(kAddrNewGeneratedLabel) % label;
        }
        return address;
    }

    int CreateNewAddress(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, IPrivateKeyKeeper::Ptr keyKeeper)
    {
        auto comment = vm[cli::NEW_ADDRESS_COMMENT].as<string>();
        auto expiration = vm[cli::EXPIRATION_TIME].as<string>();

        WalletAddress::ExpirationStatus expirationStatus;
        if (expiration == cli::EXPIRATION_TIME_24H)
        {
            expirationStatus = WalletAddress::ExpirationStatus::OneDay;
        }
        else if (expiration == cli::EXPIRATION_TIME_NEVER)
        {
            expirationStatus = WalletAddress::ExpirationStatus::Never;
        }
        else
        {
            LOG_ERROR() << boost::format(kErrorAddrExprTimeInvalid) 
                        % cli::EXPIRATION_TIME
                        % expiration;
            return -1;
        }
        
        GenerateNewAddress(walletDB, comment, keyKeeper, expirationStatus);
        return 0;
    }

    WordList GeneratePhrase()
    {
        auto phrase = createMnemonic(getEntropy(), language::en);
        BOOST_ASSERT(phrase.size() == 12);
        cout << kSeedPhraseGeneratedTitle;
        for (const auto& word : phrase)
        {
            cout << word << ';';
        }
        cout << kSeedPhraseGeneratedMessage << endl;
        return phrase;
    }

    bool ReadWalletSeed(NoLeak<uintBig>& walletSeed, const po::variables_map& vm, bool generateNew)
    {
        SecString seed;
        WordList phrase;
        if (generateNew)
        {
            LOG_INFO() << kSeedPhraseReadTitle;
            phrase = GeneratePhrase();
        }
        else if (vm.count(cli::SEED_PHRASE))
        {
            auto tempPhrase = vm[cli::SEED_PHRASE].as<string>();
            boost::algorithm::trim_if(tempPhrase, [](char ch) { return ch == ';'; });
            phrase = string_helpers::split(tempPhrase, ';');

            if (phrase.size() != WORD_COUNT
                || (vm.count(cli::IGNORE_DICTIONARY) == 0 && !isValidMnemonic(phrase, language::en)))
            {
                LOG_ERROR() << boost::format(kErrorSeedPhraseInvalid) % tempPhrase;
                return false;
            }
        }
        else
        {
            LOG_ERROR() << kErrorSeedPhraseNotProvided;
            return false;
        }

        auto buf = decodeMnemonic(phrase);
        seed.assign(buf.data(), buf.size());

        walletSeed.V = seed.hash().V;
        return true;
    }

    int ShowAddressList(const IWalletDB::Ptr& walletDB)
    {
        auto addresses = walletDB->getAddresses(true);
        array<uint8_t, 5> columnWidths{ { 20, 70, 8, 20, 21 } };

        // Comment | Address | Active | Expiration date | Created |
        cout << boost::format(kAddrListTableHead)
             % boost::io::group(left, setw(columnWidths[0]), kAddrListColumnComment)
             % boost::io::group(left, setw(columnWidths[1]), kAddrListColumnAddress)
             % boost::io::group(left, setw(columnWidths[2]), kAddrListColumnActive)
             % boost::io::group(left, setw(columnWidths[3]), kAddrListColumnExprDate)
             % boost::io::group(left, setw(columnWidths[4]), kAddrListColumnCreated)
             << std::endl;

        for (const auto& address : addresses)
        {
            auto comment = address.m_label;

            if (comment.length() > columnWidths[0])
            {
                comment = comment.substr(0, columnWidths[0] - 3) + "...";
            }

            auto expirationDateText = (address.m_duration == 0)
                ? cli::EXPIRATION_TIME_NEVER
                : format_timestamp(kTimeStampFormat3x3, address.getExpirationTime() * 1000, false);
            auto creationDateText = format_timestamp(kTimeStampFormat3x3, address.getCreateTime() * 1000, false);

            cout << boost::format(kAddrListTableBody)
             % boost::io::group(left, setw(columnWidths[0]), comment)
             % boost::io::group(left, setw(columnWidths[1]), std::to_string(address.m_walletID))
             % boost::io::group(left, boolalpha, setw(columnWidths[2]), !address.isExpired())
             % boost::io::group(left, setw(columnWidths[3]), expirationDateText)
             % boost::io::group(left, setw(columnWidths[4]), creationDateText)
             << std::endl;
        }

        return 0;
    }

    int ShowWalletInfo(const IWalletDB::Ptr& walletDB, const po::variables_map& vm)
    {
        Block::SystemState::ID stateID = {};
        walletDB->getSystemStateID(stateID);

        storage::Totals totals(*walletDB);

        const unsigned kWidth = 26; 
        cout << boost::format(kWalletSummaryFormat)

             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldCurHeight) % stateID.m_Height
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldCurStateID) % stateID.m_Hash

             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailable) % to_string(PrintableAmount(totals.Avail))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldMaturing) % to_string(PrintableAmount(totals.Maturing))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldInProgress) % to_string(PrintableAmount(totals.Incoming))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldUnavailable) % to_string(PrintableAmount(totals.Unavail))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailableCoinbase) % to_string(PrintableAmount(totals.AvailCoinbase))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalCoinbase) % to_string(PrintableAmount(totals.Coinbase))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvaliableFee) % to_string(PrintableAmount(totals.AvailFee))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalFee) % to_string(PrintableAmount(totals.Fee))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalUnspent) % to_string(PrintableAmount(totals.Unspent));

        if (vm.count(cli::TX_HISTORY))
        {
            auto txHistory = walletDB->getTxHistory();
            if (txHistory.empty())
            {
                cout << kTxHistoryEmpty << endl;
                return 0;
            }

            const array<uint8_t, 6> columnWidths{ { 20, 17, 26, 21, 33, 65} };

            cout << boost::format(kTxHistoryTableHead)
                 % boost::io::group(left, setw(columnWidths[0]), kTxHistoryColumnDatetTime)
                 % boost::io::group(left, setw(columnWidths[1]), kTxHistoryColumnDirection)
                 % boost::io::group(right, setw(columnWidths[2]), kTxHistoryColumnAmount)
                 % boost::io::group(left, setw(columnWidths[3]), kTxHistoryColumnStatus)
                 % boost::io::group(left, setw(columnWidths[4]), kTxHistoryColumnId)
                 % boost::io::group(left, setw(columnWidths[5]), kTxHistoryColumnKernelId)
                 << std::endl;

            for (auto& tx : txHistory)
            {
                cout << boost::format(kTxHistoryTableFormat)
                     % boost::io::group(left, setw(columnWidths[0]), format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                     % boost::io::group(left, setw(columnWidths[1]), (tx.m_selfTx ? kTxDirectionSelf : (tx.m_sender ? kTxDirectionOut : kTxDirectionIn)))
                     % boost::io::group(right, setw(columnWidths[2]), to_string(PrintableAmount(tx.m_amount, true)))
                     % boost::io::group(left, setw(columnWidths[3]), getTxStatus(tx))
                     % boost::io::group(left, setw(columnWidths[4]), to_hex(tx.m_txId.data(), tx.m_txId.size()))
                     % boost::io::group(left, setw(columnWidths[5]), to_string(tx.m_kernelID))
                     << std::endl;
            }
            return 0;
        }

        if (vm.count(cli::SWAP_TX_HISTORY))
        {
            auto txHistory = walletDB->getTxHistory(wallet::TxType::AtomicSwap);
            if (txHistory.empty())
            {
                cout << kSwapTxHistoryEmpty << endl;
                return 0;
            }

            const array<uint8_t, 6> columnWidths{ { 20, 26, 18, 15, 23, 33} };

            cout << boost::format(kTxHistoryTableHead)
                 % boost::io::group(left, setw(columnWidths[0]), kTxHistoryColumnDatetTime)
                 % boost::io::group(right, setw(columnWidths[1]), kTxHistoryColumnAmount)
                 % boost::io::group(right, setw(columnWidths[2]), kTxHistoryColumnSwapAmount)
                 % boost::io::group(left, setw(columnWidths[3]), kTxHistoryColumnSwapType)
                 % boost::io::group(left, setw(columnWidths[4]), kTxHistoryColumnStatus)
                 % boost::io::group(left, setw(columnWidths[5]), kTxHistoryColumnId)
                 << std::endl;

            for (auto& tx : txHistory)
            {
                Amount swapAmount = 0;
                storage::getTxParameter(*walletDB, tx.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::AtomicSwapAmount, swapAmount);
                bool isBeamSide = false;
                storage::getTxParameter(*walletDB, tx.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::AtomicSwapIsBeamSide, isBeamSide);

                AtomicSwapCoin swapCoin = AtomicSwapCoin::Unknown;
                storage::getTxParameter(*walletDB, tx.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::AtomicSwapCoin, swapCoin);

                stringstream ss;
                ss << (isBeamSide ? kBEAM : getAtomicSwapCoinText(swapCoin)) << " <--> " << (!isBeamSide ? kBEAM : getAtomicSwapCoinText(swapCoin));

                cout << boost::format(kSwapTxHistoryTableFormat)
                     % boost::io::group(left, setw(columnWidths[0]), format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                     % boost::io::group(right, setw(columnWidths[1]), to_string(PrintableAmount(tx.m_amount, true)))
                     % boost::io::group(right, setw(columnWidths[2]), swapAmount)
                     % boost::io::group(right, setw(columnWidths[3]), ss.str())
                     % boost::io::group(left, setw(columnWidths[4]), getSwapTxStatus(walletDB, tx))
                     % boost::io::group(left, setw(columnWidths[5]), to_hex(tx.m_txId.data(), tx.m_txId.size()))
                     << std::endl;
            }
            return 0;
        }

        const array<uint8_t, 6> columnWidths{ { 49, 14, 14, 18, 30, 8} };
        cout << boost::format(kCoinsTableHeadFormat)
                 % boost::io::group(left, setw(columnWidths[0]), kCoinColumnId)
                 % boost::io::group(right, setw(columnWidths[1]), kBEAM)
                 % boost::io::group(right, setw(columnWidths[2]), kGROTH)
                 % boost::io::group(left, setw(columnWidths[3]), kCoinColumnMaturity)
                 % boost::io::group(left, setw(columnWidths[4]), kCoinColumnStatus)
                 % boost::io::group(left, setw(columnWidths[5]), kCoinColumnType)
                 << std::endl;
        
        walletDB->visitCoins([&columnWidths](const Coin& c)->bool
        {
            cout << boost::format(kCoinsTableFormat)
                 % boost::io::group(left, setw(columnWidths[0]), c.toStringID())
                 % boost::io::group(right, setw(columnWidths[1]), c.m_ID.m_Value / Rules::Coin)
                 % boost::io::group(right, setw(columnWidths[2]), c.m_ID.m_Value % Rules::Coin)
                 % boost::io::group(left, setw(columnWidths[3]), (c.IsMaturityValid() ? std::to_string(static_cast<int64_t>(c.m_maturity)) : "-"))
                 % boost::io::group(left, setw(columnWidths[4]), getCoinStatus(c.m_status))
                 % boost::io::group(left, setw(columnWidths[5]), c.m_ID.m_Type)
                 << std::endl;
            return true;
        });
        return 0;
    }

    int TxDetails(const IWalletDB::Ptr& walletDB, const po::variables_map& vm)
    {
        auto txIdStr = vm[cli::TX_ID].as<string>();
        if (txIdStr.empty()) {
            LOG_ERROR() << kErrorTxIdParamReqired;
            return -1;
        }
        auto txIdVec = from_hex(txIdStr);
        TxID txId;
        if (txIdVec.size() >= 16)
            std::copy_n(txIdVec.begin(), 16, txId.begin());

        auto tx = walletDB->getTx(txId);
        if (!tx)
        {
            LOG_ERROR() << boost::format(kErrorTxWithIdNotFound) % txIdStr;
            return -1;
        }

        LOG_INFO()
            << boost::format(kTxDetailsFormat)
                % storage::TxDetailsInfo(walletDB, txId) % getTxStatus(*tx) 
            << (tx->m_status == TxStatus::Failed
                    ? boost::format(kTxDetailsFailReason) % GetFailureMessage(tx->m_failureReason)
                    : boost::format(""));

        return 0;
    }

    int ExportPaymentProof(const IWalletDB::Ptr& walletDB, const po::variables_map& vm)
    {
        auto txIdVec = from_hex(vm[cli::TX_ID].as<string>());
        TxID txId;
        if (txIdVec.size() >= 16)
            std::copy_n(txIdVec.begin(), 16, txId.begin());

        auto tx = walletDB->getTx(txId);
        if (!tx)
        {
            LOG_ERROR() << kErrorPpExportFailed;
            return -1;
        }
        if (!tx->m_sender || tx->m_selfTx)
        {
            LOG_ERROR() << kErrorPpCannotExportForReceiver;
            return -1;
        }
        if (tx->m_status != TxStatus::Completed)
        {
            LOG_ERROR() << kErrorPpExportFailedTxNotCompleted;
            return -1;
        }

        auto res = storage::ExportPaymentProof(*walletDB, txId);
        if (!res.empty())
        {
            std::string sTxt;
            sTxt.resize(res.size() * 2);

            beam::to_hex(&sTxt.front(), res.data(), res.size());
            LOG_INFO() << boost::format(kPpExportedFrom) % sTxt;
        }

        return 0;
    }

    int VerifyPaymentProof(const po::variables_map& vm)
    {
        const auto& pprofData = vm[cli::PAYMENT_PROOF_DATA];
        if (pprofData.empty())
        {
            throw std::runtime_error(kErrorPpNotProvided);
        }
        ByteBuffer buf = from_hex(pprofData.as<string>());

        if (!storage::VerifyPaymentProof(buf))
            throw std::runtime_error(kErrorPpInvalid);

        return 0;
    }

    int ExportMinerKey(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, const beam::SecString& pass)
    {
        uint32_t subKey = vm[cli::KEY_SUBKEY].as<Nonnegative<uint32_t>>().value;
        if (subKey < 1)
        {
            cout << kErrorSubkeyNotSpecified << endl;
            return -1;
        }
		Key::IKdf::Ptr pKey = MasterKey::get_Child(*walletDB->get_MasterKdf(), subKey);
        const ECC::HKdf& kdf = static_cast<ECC::HKdf&>(*pKey);

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(subKey);

        ks.Export(kdf);
        cout << boost::format(kSubKeyInfo) % subKey % ks.m_sRes << std::endl;

        return 0;
    }

    int ExportOwnerKey(const IWalletDB::Ptr& walletDB, const beam::SecString& pass)
    {
        Key::IKdf::Ptr pKey = walletDB->get_MasterKdf();
        const ECC::HKdf& kdf = static_cast<ECC::HKdf&>(*pKey);

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(0);

        ECC::HKdfPub pkdf;
        pkdf.GenerateFrom(kdf);

        ks.Export(pkdf);
        cout << boost::format(kOwnerKeyInfo) % ks.m_sRes << std::endl;

        return 0;
    }

    bool LoadDataToImport(const std::string& path, ByteBuffer& data)
    {
        FStream f;
        if (f.Open(path.c_str(), true))
        {
            size_t size = static_cast<size_t>(f.get_Remaining());
            if (size > 0)
            {
                data.resize(size);
                return f.read(data.data(), data.size()) == size;
            }
        }
        return false;
    }

    bool SaveExportedData(const ByteBuffer& data, const std::string& path)
    {
        size_t dotPos = path.find_last_of('.');
        stringstream ss;
        ss << path.substr(0, dotPos);
        ss << getTimestamp();
        if (dotPos != string::npos)
        {
            ss << path.substr(dotPos);
        }
        string timestampedPath = ss.str();
        FStream f;
        if (f.Open(timestampedPath.c_str(), false) && f.write(data.data(), data.size()) == data.size())
        {
            LOG_INFO() << kDataExportedMessage;
            return true;
        }
        LOG_ERROR() << kErrorExportDataFail;
        return false;
    }

    int ExportWalletData(const po::variables_map& vm, const IWalletDB::Ptr& walletDB)
    {
        auto s = storage::ExportDataToJson(*walletDB);
        return SaveExportedData(ByteBuffer(s.begin(), s.end()), vm[cli::IMPORT_EXPORT_PATH].as<string>()) ? 0 : -1;
    }

    int ImportWalletData(const po::variables_map& vm, const IWalletDB::Ptr& walletDB)
    {
        ByteBuffer buffer;
        if (!LoadDataToImport(vm[cli::IMPORT_EXPORT_PATH].as<string>(), buffer))
        {
            return -1;
        }
        const char* p = (char*)(&buffer[0]);
        auto keyKeeper = std::make_shared<LocalPrivateKeyKeeper>(walletDB);
        return storage::ImportDataFromJson(*walletDB, keyKeeper, p, buffer.size()) ? 0 : -1;
    }

    CoinIDList GetPreselectedCoinIDs(const po::variables_map& vm)
    {
        CoinIDList coinIDs;
        if (vm.count(cli::UTXO))
        {
            auto tempCoins = vm[cli::UTXO].as<vector<string>>();
            for (const auto& s : tempCoins)
            {
                auto csv = string_helpers::split(s, ',');
                for (const auto& v : csv)
                {
                    auto coinID = Coin::FromString(v);
                    if (coinID)
                    {
                        coinIDs.push_back(*coinID);
                    }
                }
            }
        }
        return coinIDs;
    }

    bool LoadBaseParamsForTX(const po::variables_map& vm, Amount& amount, Amount& fee, WalletID& receiverWalletID, bool checkFee)
    {
        if (vm.count(cli::RECEIVER_ADDR) == 0)
        {
            LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }
        if (vm.count(cli::AMOUNT) == 0)
        {
            LOG_ERROR() << kErrorAmountMissing;
            return false;
        }

        receiverWalletID.FromHex(vm[cli::RECEIVER_ADDR].as<string>());

        auto signedAmount = vm[cli::AMOUNT].as<Positive<double>>().value;
        if (signedAmount < 0)
        {
            LOG_ERROR() << kErrorNegativeAmount;
            return false;
        }

        signedAmount *= Rules::Coin; // convert beams to groths

        amount = static_cast<ECC::Amount>(std::round(signedAmount));
        if (amount == 0)
        {
            LOG_ERROR() << kErrorZeroAmount;
            return false;
        }

        fee = vm[cli::FEE].as<Nonnegative<Amount>>().value;
        if (checkFee && fee < cli::kMinimumFee)
        {
            LOG_ERROR() << kErrorFeeToLow;
            return false;
        }

        return true;
    }

    SwapSecondSideChainType ParseSwapSecondSideChainType(const po::variables_map& vm)
    {
        SwapSecondSideChainType swapSecondSideChainType = SwapSecondSideChainType::Unknown;
        if (vm.count(cli::SWAP_NETWORK) > 0)
        {
            swapSecondSideChainType = SwapSecondSideChainTypeFromString(vm[cli::SWAP_NETWORK].as<string>());
            if (swapSecondSideChainType == SwapSecondSideChainType::Unknown)
            {
                throw std::runtime_error(kErrorUnknownSecondSideChainForSwap);
            }
        }
        return swapSecondSideChainType;
    }

    boost::optional<BitcoinOptions> ParseBitcoinOptions(const po::variables_map& vm)
    {
        if (vm.count(cli::BTC_NODE_ADDR) > 0 || vm.count(cli::BTC_USER_NAME) > 0 || vm.count(cli::BTC_PASS) > 0)
        {
            BitcoinOptions btcOptions;

            string btcNodeUri = vm[cli::BTC_NODE_ADDR].as<string>();
            if (!btcOptions.m_address.resolve(btcNodeUri.c_str()))
            {
                throw std::runtime_error((boost::format(kErrorBTCNodeAddrNotResolved) % btcNodeUri).str());
            }

            if (vm.count(cli::BTC_USER_NAME) == 0)
            {
                throw std::runtime_error(kErrorBTCNodeUserNameUnspecified);
            }

            btcOptions.m_userName = vm[cli::BTC_USER_NAME].as<string>();

            // TODO roman.strilets: use SecString instead of std::string
            if (vm.count(cli::BTC_PASS) == 0)
            {
                throw std::runtime_error(kErrorBTCNodePwdNotProvided);
            }

            btcOptions.m_pass = vm[cli::BTC_PASS].as<string>();

            if (vm.count(cli::SWAP_FEERATE) == 0)
            {
                throw std::runtime_error(kErrorSwapFeeRateMissing);
            }

            btcOptions.m_feeRate = vm[cli::SWAP_FEERATE].as<Positive<Amount>>().value;

            auto swapSecondSideChainType = ParseSwapSecondSideChainType(vm);
            if (swapSecondSideChainType != SwapSecondSideChainType::Unknown)
            {
                btcOptions.m_chainType = swapSecondSideChainType;
            }

            return btcOptions;
        }

        return boost::optional<BitcoinOptions>{};
    }

    boost::optional<LitecoinOptions> ParseLitecoinOptions(const po::variables_map& vm)
    {
        if (vm.count(cli::LTC_NODE_ADDR) > 0 || vm.count(cli::LTC_USER_NAME) > 0 || vm.count(cli::LTC_PASS) > 0)
        {
            LitecoinOptions ltcOptions;

            string ltcNodeUri = vm[cli::LTC_NODE_ADDR].as<string>();
            if (!ltcOptions.m_address.resolve(ltcNodeUri.c_str()))
            {
                throw std::runtime_error((boost::format(kErrorLTCNodeAddrNotResolved) % ltcNodeUri).str());
            }

            if (vm.count(cli::LTC_USER_NAME) == 0)
            {
                throw std::runtime_error(kErrorLTCNodeUserNameUnspecified);
            }

            ltcOptions.m_userName = vm[cli::LTC_USER_NAME].as<string>();

            // TODO roman.strilets: use SecString instead of std::string
            if (vm.count(cli::LTC_PASS) == 0)
            {
                throw std::runtime_error(kErrorLTCNodePwdNotProvided);
            }

            ltcOptions.m_pass = vm[cli::LTC_PASS].as<string>();

            if (vm.count(cli::SWAP_FEERATE) == 0)
            {
                throw std::runtime_error(kErrorSwapFeeRateMissing);
            }
            ltcOptions.m_feeRate = vm[cli::SWAP_FEERATE].as<Positive<Amount>>().value;

            auto swapSecondSideChainType = ParseSwapSecondSideChainType(vm);
            if (swapSecondSideChainType != SwapSecondSideChainType::Unknown)
            {
                ltcOptions.m_chainType = swapSecondSideChainType;
            }

            return ltcOptions;
        }

        return boost::optional<LitecoinOptions>{};
    }

    boost::optional<QtumOptions> ParseQtumOptions(const po::variables_map& vm)
    {
        if (vm.count(cli::QTUM_NODE_ADDR) > 0 || vm.count(cli::QTUM_USER_NAME) > 0 || vm.count(cli::QTUM_PASS) > 0)
        {
            QtumOptions qtumOptions;

            string qtumNodeUri = vm[cli::QTUM_NODE_ADDR].as<string>();
            if (!qtumOptions.m_address.resolve(qtumNodeUri.c_str()))
            {
                throw std::runtime_error((boost::format(kErrorQTUMNodeAddrNotResolved) % qtumNodeUri).str());
            }

            if (vm.count(cli::QTUM_USER_NAME) == 0)
            {
                throw std::runtime_error(kErrorQTUMNodeUserNameUnspecified);
            }

            qtumOptions.m_userName = vm[cli::QTUM_USER_NAME].as<string>();

            // TODO roman.strilets: use SecString instead of std::string
            if (vm.count(cli::QTUM_PASS) == 0)
            {
                throw std::runtime_error(kErrorQTUMNodePwdNotProvided);
            }

            qtumOptions.m_pass = vm[cli::QTUM_PASS].as<string>();

            if (vm.count(cli::SWAP_FEERATE) == 0)
            {
                throw std::runtime_error(kErrorSwapFeeRateMissing);
            }
            qtumOptions.m_feeRate = vm[cli::SWAP_FEERATE].as<Positive<Amount>>().value;

            auto swapSecondSideChainType = ParseSwapSecondSideChainType(vm);
            if (swapSecondSideChainType != SwapSecondSideChainType::Unknown)
            {
                qtumOptions.m_chainType = swapSecondSideChainType;
            }

            return qtumOptions;
        }

        return boost::optional<QtumOptions>{};
    }
}

io::Reactor::Ptr reactor;

static const unsigned LOG_ROTATION_PERIOD_SEC = 3*60*60; // 3 hours

int main_impl(int argc, char* argv[])
{
    beam::Crash::InstallHandler(NULL);

    try
    {
        auto [options, visibleOptions] = createOptionsDescription(GENERAL_OPTIONS | WALLET_OPTIONS);

        po::variables_map vm;
        try
        {
            vm = getOptions(argc, argv, kDefaultConfigFile, options, true);
        }
        catch (const po::invalid_option_value& e)
        {
            cout << e.what() << std::endl;
            return 0;
        }
        catch (const NonnegativeOptionException& e)
        {
            cout << e.what() << std::endl;
            return 0;
        }
        catch (const PositiveOptionException& e)
        {
            cout << e.what() << std::endl;
            return 0;
        }
        catch (const po::error& e)
        {
            cout << e.what() << std::endl;
            printHelp(visibleOptions);

            return 0;
        }

        if (vm.count(cli::HELP))
        {
            printHelp(visibleOptions);

            return 0;
        }

        if (vm.count(cli::VERSION))
        {
            cout << PROJECT_VERSION << endl;
            return 0;
        }

        if (vm.count(cli::GIT_COMMIT_HASH))
        {
            cout << GIT_COMMIT_HASH << endl;
            return 0;
        }

        int logLevel = getLogLevel(cli::LOG_LEVEL, vm, LOG_LEVEL_DEBUG);
        int fileLogLevel = getLogLevel(cli::FILE_LOG_LEVEL, vm, LOG_LEVEL_DEBUG);

#define LOG_FILES_DIR "logs"
#define LOG_FILES_PREFIX "wallet_"

        const auto path = boost::filesystem::system_complete(LOG_FILES_DIR);
        auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, LOG_FILES_PREFIX, path.string());

        try
        {
            po::notify(vm);

            unsigned logCleanupPeriod = vm[cli::LOG_CLEANUP_DAYS].as<uint32_t>() * 24 * 3600;

            clean_old_logfiles(LOG_FILES_DIR, LOG_FILES_PREFIX, logCleanupPeriod);

            Rules::get().UpdateChecksum();

            {
                reactor = io::Reactor::create();
                io::Reactor::Scope scope(*reactor);

                io::Reactor::GracefulIntHandler gih(*reactor);

                LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, logCleanupPeriod);

                {
                    if (vm.count(cli::COMMAND) == 0)
                    {
                        LOG_ERROR() << kErrorCommandNotSpecified;
                        printHelp(visibleOptions);
                        return 0;
                    }

                    auto command = vm[cli::COMMAND].as<string>();

                    {
                        const string commands[] =
                        {
                            cli::INIT,
                            cli::RESTORE,
                            cli::SEND,
                            cli::LISTEN,
                            cli::TREASURY,
                            cli::INFO,
                            cli::EXPORT_MINER_KEY,
                            cli::EXPORT_OWNER_KEY,
                            cli::NEW_ADDRESS,
                            cli::CANCEL_TX,
                            cli::DELETE_TX,
                            cli::CHANGE_ADDRESS_EXPIRATION,
                            cli::TX_DETAILS,
                            cli::PAYMENT_PROOF_EXPORT,
                            cli::PAYMENT_PROOF_VERIFY,
                            cli::GENERATE_PHRASE,
                            cli::WALLET_ADDRESS_LIST,
                            cli::WALLET_RESCAN,
                            cli::IMPORT_DATA,
                            cli::EXPORT_DATA,
                            cli::SWAP_INIT,
                            cli::SWAP_LISTEN
                        };

                        if (find(begin(commands), end(commands), command) == end(commands))
                        {
                            LOG_ERROR() << boost::format(kErrorCommandUnknown) % command;
                            return -1;
                        }
                    }

                    if (command == cli::GENERATE_PHRASE)
                    {
                        GeneratePhrase();
                        return 0;
                    }

                    LOG_INFO() << boost::format(kVersionInfo) % PROJECT_VERSION % BRANCH_NAME;
                    LOG_INFO() << boost::format(kRulesSignatureInfo) % Rules::get().get_SignatureStr();

                    bool coldWallet = vm.count(cli::COLD_WALLET) > 0;

                    if (coldWallet && command == cli::RESTORE)
                    {
                        LOG_ERROR() << kErrorCantRestoreColdWallet;
                        return -1;
                    }

                    BOOST_ASSERT(vm.count(cli::WALLET_STORAGE) > 0);
                    auto walletPath = vm[cli::WALLET_STORAGE].as<string>();

                    if (!WalletDB::isInitialized(walletPath) && (command != cli::INIT && command != cli::RESTORE))
                    {
                        LOG_ERROR() << kErrorWalletNotInitialized;
                        return -1;
                    }
                    else if (WalletDB::isInitialized(walletPath) && (command == cli::INIT || command == cli::RESTORE))
                    {
                        bool isDirectory = false;
                        #ifdef WIN32
                                isDirectory = boost::filesystem::is_directory(Utf8toUtf16(walletPath.c_str()));
                        #else
                                isDirectory = boost::filesystem::is_directory(walletPath);
                        #endif

                        if (isDirectory)
                        {
                            walletPath.append("/wallet.db");
                        }
                        else
                        {
                            LOG_ERROR() << kErrorWalletAlreadyInitialized;
                            return -1;
                        }                  
                    }

                    LOG_INFO() << kStartMessage;

                    SecString pass;
                    if (!beam::read_wallet_pass(pass, vm))
                    {
                        LOG_ERROR() << kErrorWalletPwdNotProvided;
                        return -1;
                    }

                    if ((command == cli::INIT || command == cli::RESTORE) && vm.count(cli::PASS) == 0)
                    {
                        if (!beam::confirm_wallet_pass(pass))
                        {
                            LOG_ERROR() << kErrorWalletPwdNotMatch;
                            return -1;
                        }
                    }

                    if (command == cli::INIT || command == cli::RESTORE)
                    {
                        NoLeak<uintBig> walletSeed;
                        walletSeed.V = Zero;
                        if (!ReadWalletSeed(walletSeed, vm, command == cli::INIT))
                        {
                            LOG_ERROR() << kErrorSeedPhraseFail;
                            return -1;
                        }
                        auto walletDB = WalletDB::init(walletPath, pass, walletSeed, reactor, coldWallet);
                        IPrivateKeyKeeper::Ptr keyKeeper = make_shared<LocalPrivateKeyKeeper>(walletDB);
                        if (walletDB)
                        {
                            LOG_INFO() << kWalletCreatedMessage;
                            CreateNewAddress(vm, walletDB, keyKeeper);
                            return 0;
                        }
                        else
                        {
                            LOG_ERROR() << kErrorWalletNotCreated;
                            return -1;
                        }
                    }

                    auto walletDB = WalletDB::open(walletPath, pass, reactor);
                    if (!walletDB)
                    {
                        LOG_ERROR() << kErrorCantOpenWallet;
                        return -1;
                    }

                    IPrivateKeyKeeper::Ptr keyKeeper = make_shared<LocalPrivateKeyKeeper>(walletDB);

                    const auto& currHeight = walletDB->getCurrentHeight();
                    const auto& fork1Height = Rules::get().pForks[1].m_Height;
                    const bool isFork1 = currHeight >= fork1Height;

                    if (command == cli::CHANGE_ADDRESS_EXPIRATION)
                    {
                        return ChangeAddressExpiration(vm, walletDB);
                    }

                    if (command == cli::EXPORT_MINER_KEY)
                    {
                        return ExportMinerKey(vm, walletDB, pass);
                    }

                    if (command == cli::EXPORT_OWNER_KEY)
                    {
                        return ExportOwnerKey(walletDB, pass);
                    }

                    if (command == cli::EXPORT_DATA)
                    {
                        return ExportWalletData(vm, walletDB);
                    }

                    if (command == cli::IMPORT_DATA)
                    {
                        return ImportWalletData(vm, walletDB);
                    }

                    {
                        const auto& var = vm[cli::PAYMENT_PROOF_REQUIRED];
                        if (!var.empty())
                        {
                            bool b = var.as<bool>();
                            uint8_t n = b ? 1 : 0;
                            storage::setVar(*walletDB, storage::g_szPaymentProofRequired, n);

                            cout << boost::format(kPpRequired) % static_cast<uint32_t>(n) << std::endl;
                            return 0;
                        }
                    }

                    if (command == cli::NEW_ADDRESS)
                    {
                        if (!CreateNewAddress(vm, walletDB, keyKeeper))
                        {
                            return -1;
                        }

                        if (!vm.count(cli::LISTEN))
                        {
                            return 0;
                        }
                    }

                    LOG_INFO() << kWalletOpenedMessage;

                    if (command == cli::TREASURY)
                    {
                        return HandleTreasury(vm, *walletDB->get_MasterKdf());
                    }

                    if (command == cli::INFO)
                    {
                        return ShowWalletInfo(walletDB, vm);
                    }

                    if (command == cli::TX_DETAILS)
                    {
                        return TxDetails(walletDB, vm);
                    }

                    if (command == cli::PAYMENT_PROOF_EXPORT)
                    {
                        return ExportPaymentProof(walletDB, vm);
                    }

                    if (command == cli::PAYMENT_PROOF_VERIFY)
                    {
                        return VerifyPaymentProof(vm);
                    }

                    if (command == cli::WALLET_ADDRESS_LIST)
                    {
                        return ShowAddressList(walletDB);
                    }

                    boost::optional<BitcoinOptions> btcOptions = ParseBitcoinOptions(vm);
                    boost::optional<LitecoinOptions> ltcOptions = ParseLitecoinOptions(vm);
                    boost::optional<QtumOptions> qtumOptions = ParseQtumOptions(vm);

                    /// HERE!!
                    io::Address receiverAddr;
                    Amount amount = 0;
                    Amount fee = 0;
                    WalletID receiverWalletID(Zero);
                    bool isTxInitiator = command == cli::SEND;
                    if (isTxInitiator && !LoadBaseParamsForTX(vm, amount, fee, receiverWalletID, isFork1))
                    {
                        return -1;
                    }

                    bool is_server = command == cli::LISTEN || vm.count(cli::LISTEN);

                    boost::optional<TxID> currentTxID;
                    auto txCompleteAction = [&currentTxID](const TxID& txID)
                    {
                        if (currentTxID.is_initialized() && currentTxID.get() != txID)
                        {
                            return;
                        }
                        io::Reactor::get_Current().stop();
                    };

                    Wallet wallet{ walletDB, keyKeeper, is_server ? Wallet::TxCompletedAction() : txCompleteAction,
                                            !coldWallet ? Wallet::UpdateCompletedAction() : []() {io::Reactor::get_Current().stop(); } };
                    {
                        wallet::AsyncContextHolder holder(wallet);
                        if (!coldWallet)
                        {
                            if (vm.count(cli::NODE_ADDR) == 0)
                            {
                                LOG_ERROR() << kErrorNodeAddrNotSpecified;
                                return -1;
                            }

                            string nodeURI = vm[cli::NODE_ADDR].as<string>();
                            io::Address nodeAddress;
                            if (!nodeAddress.resolve(nodeURI.c_str()))
                            {
                                LOG_ERROR() << boost::format(kErrorNodeAddrUnresolved) % nodeURI;
                                return -1;
                            }

                            auto nnet = make_shared<proto::FlyClient::NetworkStd>(wallet);
                            nnet->m_Cfg.m_PollPeriod_ms = vm[cli::NODE_POLL_PERIOD].as<Nonnegative<uint32_t>>().value;
                            if (nnet->m_Cfg.m_PollPeriod_ms)
                            {
                                LOG_INFO() << boost::format(kNodePoolPeriod) % nnet->m_Cfg.m_PollPeriod_ms;
                                uint32_t timeout_ms = std::max(Rules::get().DA.Target_s * 1000, nnet->m_Cfg.m_PollPeriod_ms);
                                if (timeout_ms != nnet->m_Cfg.m_PollPeriod_ms)
                                {
                                    LOG_INFO() << boost::format(kNodePoolPeriodRounded) % timeout_ms;
                                }
                            }
                            uint32_t responceTime_s = Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
                            if (nnet->m_Cfg.m_PollPeriod_ms >= responceTime_s * 1000)
                            {
                                LOG_WARNING() << boost::format(kErrorNodePoolPeriodTooMuch) % uint32_t(responceTime_s / 3600);
                            }
                            nnet->m_Cfg.m_vNodes.push_back(nodeAddress);
                            nnet->Connect();
                            wallet.AddMessageEndpoint(make_shared<WalletNetworkViaBbs>(wallet, nnet, walletDB, keyKeeper));
                            wallet.SetNodeEndpoint(nnet);
                        }
                        else
                        {
                            wallet.AddMessageEndpoint(make_shared<ColdWalletMessageEndpoint>(wallet, walletDB, keyKeeper));
                        }

                        if (btcOptions.is_initialized())
                        {
                            wallet.initBitcoin(io::Reactor::get_Current(), btcOptions.get());
                        }

                        if (ltcOptions.is_initialized())
                        {
                            wallet.initLitecoin(io::Reactor::get_Current(), ltcOptions.get());
                        }

                        if (qtumOptions.is_initialized())
                        {
                            wallet.initQtum(io::Reactor::get_Current(), qtumOptions.get());
                        }

                        if (command == cli::SWAP_INIT || command == cli::SWAP_LISTEN)
                        {
                            if (vm.count(cli::SWAP_AMOUNT) == 0)
                            {
                                LOG_ERROR() << kErrorSwapAmountMissing;
                                return -1;
                            }

                            Amount swapAmount = vm[cli::SWAP_AMOUNT].as<Positive<Amount>>().value;

                            SwapSecondSideChainType secondSideChainType = SwapSecondSideChainType::Mainnet;
                            wallet::AtomicSwapCoin swapCoin = wallet::AtomicSwapCoin::Bitcoin;

                            if (vm.count(cli::SWAP_COIN) > 0)
                            {
                                swapCoin = wallet::from_string(vm[cli::SWAP_COIN].as<string>());

                                if (swapCoin == wallet::AtomicSwapCoin::Unknown)
                                {
                                    LOG_ERROR() << kErrorSwapCoinUnknown;
                                    return -1;
                                }
                            }

                            if (swapCoin == wallet::AtomicSwapCoin::Bitcoin)
                            {
                                if (!btcOptions.is_initialized() || btcOptions->m_userName.empty() || btcOptions->m_pass.empty() || btcOptions->m_address.empty())
                                {
                                    LOG_ERROR() << kErrorNoBTCNodeCredentials;
                                    return -1;
                                }

                                if (!BitcoinSide::CheckAmount(swapAmount, btcOptions->m_feeRate))
                                {
                                    LOG_ERROR() << kErrorSwapAmountTooLow;
                                    return -1;
                                }
                                secondSideChainType = btcOptions->m_chainType;
                            }
                            else if (swapCoin == wallet::AtomicSwapCoin::Litecoin)
                            {
                                if (!ltcOptions.is_initialized() || ltcOptions->m_userName.empty() || ltcOptions->m_pass.empty() || ltcOptions->m_address.empty())
                                {
                                    LOG_ERROR() << kErrorNoLTCNodeCredentials;
                                    return -1;
                                }
                                if (!LitecoinSide::CheckAmount(swapAmount, ltcOptions->m_feeRate))
                                {
                                    LOG_ERROR() << kErrorSwapAmountTooLow;
                                    return -1;
                                }
                                secondSideChainType = ltcOptions->m_chainType;
                            }
                            else
                            {
                                if (!qtumOptions.is_initialized() || qtumOptions->m_userName.empty() || qtumOptions->m_pass.empty() || qtumOptions->m_address.empty())
                                {
                                    LOG_ERROR() << kErrorNoQTUMNodeCredentials;
                                    return -1;
                                }
                                if (!QtumSide::CheckAmount(swapAmount, qtumOptions->m_feeRate))
                                {
                                    LOG_ERROR() << kErrorSwapAmountTooLow;
                                    return -1;
                                }
                                secondSideChainType = qtumOptions->m_chainType;
                            }
                            
                            bool isBeamSide = (vm.count(cli::SWAP_BEAM_SIDE) != 0);

                            if (command == cli::SWAP_INIT)
                            {
                                if (!LoadBaseParamsForTX(vm, amount, fee, receiverWalletID, isFork1))
                                {
                                    return -1;
                                }

                                if (vm.count(cli::SWAP_AMOUNT) == 0)
                                {
                                    LOG_ERROR() << kErrorSwapAmountMissing;
                                    return -1;
                                }

                                if (amount <= kMinFeeInGroth)
                                {
                                    LOG_ERROR() << kErrorSwapAmountTooLow;
                                    return -1;
                                }

                                WalletAddress senderAddress = GenerateNewAddress(walletDB, "", keyKeeper);

                                currentTxID = wallet.swap_coins(senderAddress.m_walletID, receiverWalletID, 
                                    move(amount), move(fee), swapCoin, swapAmount, secondSideChainType, isBeamSide);
                            }

                            if (command == cli::SWAP_LISTEN)
                            {
                                if (vm.count(cli::AMOUNT) == 0)
                                {
                                    LOG_ERROR() << kErrorAmountMissing;
                                    return false;
                                }

                                auto signedAmount = vm[cli::AMOUNT].as<Positive<double>>().value;

                                signedAmount *= Rules::Coin; // convert beams to coins

                                amount = static_cast<ECC::Amount>(std::round(signedAmount));
                                if (amount == 0)
                                {
                                    LOG_ERROR() << kErrorUnableSendZeroCoin;
                                    return false;
                                }

                                if (amount <= kMinFeeInGroth)
                                {
                                    LOG_ERROR() << kErrorAmountTooLow;
                                    return -1;
                                }
                                wallet.initSwapConditions(amount, swapAmount, swapCoin, isBeamSide, secondSideChainType);
                            }
                        }

                        if (isTxInitiator)
                        {
                            WalletAddress senderAddress = GenerateNewAddress(walletDB, "", keyKeeper);
                            CoinIDList coinIDs = GetPreselectedCoinIDs(vm);
                            currentTxID = wallet.transfer_money(senderAddress.m_walletID, receiverWalletID, move(amount), move(fee), coinIDs, command == cli::SEND, kDefaultTxLifetime, kDefaultTxResponseTime, {}, true);
                        }

                        bool deleteTx = command == cli::DELETE_TX;
                        if (command == cli::CANCEL_TX || deleteTx)
                        {
                            auto txIdVec = from_hex(vm[cli::TX_ID].as<string>());
                            TxID txId;
                            std::copy_n(txIdVec.begin(), 16, txId.begin());
                            auto tx = walletDB->getTx(txId);

                            if (tx)
                            {
                                if (deleteTx)
                                {
                                    if (tx->canDelete())
                                    {
                                        wallet.DeleteTransaction(txId);
                                        return 0;
                                    }
                                    else
                                    {
                                        LOG_ERROR() << kErrorTxStatusInvalid;
                                        return -1;
                                    }
                                }
                                else
                                {
                                    if (tx->canCancel())
                                    {
                                        currentTxID = txId;
                                        wallet.CancelTransaction(txId);
                                    }
                                    else
                                    {
                                        LOG_ERROR() << kErrorTxStatusInvalid;
                                        return -1;
                                    }
                                }
                            }
                            else
                            {
                                LOG_ERROR() << kErrorTxIdUnknown;
                                return -1;
                            }
                        }

                        if (command == cli::WALLET_RESCAN)
                        {
                            wallet.Refresh();
                        }
                    }
                    io::Reactor::get_Current().run();
                }
            }
        }
        catch (const AddressExpiredException&)
        {
        }
        catch (const FailToStartSwapException&)
        {
        }
        catch (const po::invalid_option_value& e)
        {
            cout << e.what() << std::endl;
            return 0;
        }
        catch (const NonnegativeOptionException& e)
        {
            cout << e.what() << std::endl;
            return 0;
        }
        catch (const PositiveOptionException& e)
        {
            cout << e.what() << std::endl;
            return 0;
        }
        catch (const po::error& e)
        {
            LOG_ERROR() << e.what();
            printHelp(visibleOptions);
        }
        catch (const std::runtime_error& e)
        {
            LOG_ERROR() << e.what();
        }
    }
    catch (const std::exception& e)
    {
        std::cout << e.what() << std::endl;
    }

    return 0;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    return main_impl(argc, argv);
#else
    block_sigpipe();
    auto f = std::async(
        std::launch::async,
        [argc, argv]() -> int {
            // TODO: this hungs app on OSX
            //lock_signals_in_this_thread();
            int ret = main_impl(argc, argv);
            kill(0, SIGINT);
            return ret;
        }
    );

    wait_for_termination(0);

    if (reactor) reactor->stop();

    return f.get();
#endif
}
