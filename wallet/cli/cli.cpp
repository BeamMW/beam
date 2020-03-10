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

#include "wallet/core/common.h"
#include "wallet/core/wallet_network.h"
#include "core/common.h"
#include "wallet/core/common_utils.h"
#include "wallet/core/wallet.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/core/secstring.h"
#include "wallet/core/strings_resources.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bitcoin.h"
#include "wallet/transactions/swaps/bridges/litecoin/electrum.h"
#include "wallet/transactions/swaps/bridges/denarius/electrum.h"
#include "wallet/transactions/swaps/bridges/qtum/electrum.h"
#include "wallet/transactions/swaps/bridges/litecoin/litecoin.h"
#include "wallet/transactions/swaps/bridges/denarius/denarius.h"
#include "wallet/transactions/swaps/bridges/qtum/qtum.h"

#include "wallet/transactions/swaps/common.h"
#include "wallet/transactions/swaps/utils.h"
#include "wallet/transactions/assets/assets_reg_creators.h"
#include "keykeeper/local_private_key_keeper.h"
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "core/treasury.h"
#include "core/block_rw.h"
//#include "unittests/util.h"
#include "mnemonic/mnemonic.h"
#include "utility/string_helpers.h"
#include "version.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include "utility/cli/options.h"
#include "utility/log_rotation.h"
#include "utility/helpers.h"

#ifdef BEAM_LASER_SUPPORT
#include "laser.h"
#include "wallet/laser/mediator.h"
#endif  // BEAM_LASER_SUPPORT

#include <boost/assert.hpp> 
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <iomanip>
#include <iterator>
#include <future>
#include <core/block_crypt.h>

using namespace std;
using namespace beam;
using namespace beam::wallet;
using namespace ECC;

namespace beam
{
    const char kElectrumSeparateSymbol = ' ';

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
        case Coin::Consumed: ss << kCoinStatusConsumed; break;
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
        case TxStatus::Registering: return tx.m_selfTx ? kTxStatusSendingToOwn : kTxStatusInProgress;
        case TxStatus::Canceled: return kTxStatusCancelled;
        case TxStatus::Completed:
        {
            switch (tx.m_txType)
            {
                case TxType::AssetIssue: return kTxStatusIssued;
                case TxType::AssetConsume: return kTxStatusConsumed;
                case TxType::AssetReg: return kTxStatusRegistered;
                case TxType::AssetUnreg: return kTxStatusUnregistered;
                case TxType::AssetInfo: return kTxStatusInfoProvided;
                default:
                {
                    if (tx.m_selfTx) return kTxStatusSentToOwn;
                    return tx.m_sender ? kTxStatusSent : kTxStatusReceived;
                }
            }
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
        wallet::AtomicSwapTransaction::State state = wallet::AtomicSwapTransaction::State::Initial;
        storage::getTxParameter(*walletDB, tx.m_txId, wallet::TxParameterID::State, state);

        return wallet::getSwapTxStatus(state);
    }
}  // namespace beam
namespace
{
    SecString GetPassword(const po::variables_map& vm);

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

    IWalletDB::Ptr OpenDataBase(const po::variables_map& vm, const SecString& pass)
    {
        BOOST_ASSERT(vm.count(cli::WALLET_STORAGE) > 0);
        auto walletPath = vm[cli::WALLET_STORAGE].as<string>();

        if (!WalletDB::isInitialized(walletPath))
        {
            throw std::runtime_error(kErrorWalletNotInitialized);
        }

        auto walletDB = WalletDB::open(walletPath, pass);
        LOG_INFO() << kWalletOpenedMessage;
        return walletDB;
    }

    IWalletDB::Ptr OpenDataBase(const po::variables_map& vm)
    {
        return OpenDataBase(vm, GetPassword(vm));
    }

    int HandleTreasury(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        Key::IKdf::Ptr pMaster = walletDB->get_MasterKdf();
        if (!pMaster)
        {
            cout << "Can't handle treasury without master key" << endl;
            return -1;
        }
        Key::IKdf& kdf = *pMaster;
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

                    for (Treasury::EntryMap::const_iterator it = tres.m_Entries.begin(); tres.m_Entries.end() != it; ++it)
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
                cout << boost::format(kTreasuryRecoveredCoin) % coin.m_Cid % coin.m_Incubation << std::endl;

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

    int ChangeAddressExpiration(const po::variables_map& vm)
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

        auto walletDB = OpenDataBase(vm);
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

    bool CreateNewAddress(const po::variables_map& vm,
                         const IWalletDB::Ptr& walletDB,
                         const std::string& defaultComment = "")
    {
        auto comment = defaultComment.empty() 
            ? vm[cli::NEW_ADDRESS_COMMENT].as<string>()
            : defaultComment;
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
            return false;
        }
        
        GenerateNewAddress(walletDB, comment, expirationStatus);
        return true;
    }

    bool LoadReceiverParams(const po::variables_map& , TxParameters& );
    bool ReadWalletSeed(NoLeak<uintBig>& walletSeed, const po::variables_map& vm, bool generateNew);

    SecString GetPassword(const po::variables_map& vm)
    {
        SecString pass;
        if (!beam::read_wallet_pass(pass, vm))
        {
            throw std::runtime_error(kErrorWalletPwdNotProvided);
        }
        return pass;
    }

    int InitDataBase(const po::variables_map& vm, bool generateNewSeed)
    {
        BOOST_ASSERT(vm.count(cli::WALLET_STORAGE) > 0);
        auto walletPath = vm[cli::WALLET_STORAGE].as<string>();

        if (WalletDB::isInitialized(walletPath))
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

        SecString pass = GetPassword(vm);

        if (vm.count(cli::PASS) == 0 && !beam::confirm_wallet_pass(pass))
        {
            LOG_ERROR() << kErrorWalletPwdNotMatch;
            return -1;
        }

        NoLeak<uintBig> walletSeed;
        walletSeed.V = Zero;
        if (!ReadWalletSeed(walletSeed, vm, generateNewSeed))
        {
            LOG_ERROR() << kErrorSeedPhraseFail;
            return -1;
        }
        
        auto walletDB = WalletDB::init(walletPath, pass, walletSeed);
        if (walletDB)
        {
            LOG_INFO() << kWalletCreatedMessage;
            CreateNewAddress(vm, walletDB, kDefaultAddrLabel);
            return 0;
        }
        LOG_ERROR() << kErrorWalletNotCreated;
        return -1;
    }

    int InitWallet(const po::variables_map& vm)
    {
        return InitDataBase(vm, true);
    }

    int RestoreWallet(const po::variables_map& vm)
    {
        return InitDataBase(vm, false);
    }

    int GetToken(const po::variables_map& vm)
    {
        TxParameters params;
        if (vm.find(cli::RECEIVER_ADDR) != vm.end())
        {
            auto receiver = vm[cli::RECEIVER_ADDR].as<string>();
            bool isValid = true;
            WalletID walletID;
            ByteBuffer buffer = from_hex(receiver, &isValid);
            if (!isValid || !walletID.FromBuf(buffer))
            {
                LOG_ERROR() << "Invalid address";
                return -1;
            }
            auto walletDB = OpenDataBase(vm);
            auto address = walletDB->getAddress(walletID);
            if (!address)
            {
                LOG_ERROR() << "Cannot generate token, there is no address";
                return -1;
            }
            if (address->isExpired())
            {
                LOG_ERROR() << "Cannot generate token, address is expired";
                return -1;
            }
            params.SetParameter(TxParameterID::PeerID, walletID);
            params.SetParameter(TxParameterID::PeerSecureWalletID, address->m_Identity);
        }
        else
        {
            auto walletDB = OpenDataBase(vm);
            WalletAddress address = GenerateNewAddress(walletDB, "");
            
            params.SetParameter(TxParameterID::PeerID, address.m_walletID);
            params.SetParameter(TxParameterID::PeerSecureWalletID, address.m_Identity);
        }

        params.SetParameter(beam::wallet::TxParameterID::TransactionType, beam::wallet::TxType::Simple);
        LOG_INFO() << "token: " << to_string(params);
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

    int GeneratePhrase(const po::variables_map& vm)
    {
        GeneratePhrase();
        return 0;
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

    int ShowAddressList(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
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

    void ShowAssetInfo(const storage::Totals::AssetTotals& totals)
    {
        const unsigned kWidth = 26;
        cout << boost::format(kWalletAssetSummaryFormat)
             % totals.AssetId
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailable) % to_string(PrintableAmount(totals.Avail, false, kAmountASSET, kAmountAGROTH))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldInProgress) % to_string(PrintableAmount(totals.Incoming, false, kAmountASSET, kAmountAGROTH))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldUnavailable) % to_string(PrintableAmount(totals.Unavail, false, kAmountASSET, kAmountAGROTH))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalUnspent) % to_string(PrintableAmount(totals.Unspent, false, kAmountASSET, kAmountAGROTH));
    }

    void ShowAssetCoins(const IWalletDB::Ptr& walletDB, Asset::ID assetId, const char* coin, const char* groth)
    {
        const array<uint8_t, 6> columnWidths{ { 49, 14, 14, 18, 30, 8} };
        cout << boost::format(kCoinsTableHeadFormat)
                 % boost::io::group(left, setw(columnWidths[0]), kCoinColumnId)
                 % boost::io::group(right, setw(columnWidths[1]), coin)
                 % boost::io::group(right, setw(columnWidths[2]), groth)
                 % boost::io::group(left, setw(columnWidths[3]), kCoinColumnMaturity)
                 % boost::io::group(left, setw(columnWidths[4]), kCoinColumnStatus)
                 % boost::io::group(left, setw(columnWidths[5]), kCoinColumnType)
                 << std::endl;

        walletDB->visitCoins([&columnWidths, &assetId](const Coin& c)->bool
        {
            if (c.m_ID.m_AssetID == assetId) {
                cout << boost::format(kCoinsTableFormat)
                        % boost::io::group(left, setw(columnWidths[0]), c.toStringID())
                        % boost::io::group(right, setw(columnWidths[1]), c.m_ID.m_Value / Rules::Coin)
                        % boost::io::group(right, setw(columnWidths[2]), c.m_ID.m_Value % Rules::Coin)
                        % boost::io::group(left, setw(columnWidths[3]),
                                           (c.IsMaturityValid() ? std::to_string(static_cast<int64_t>(c.m_maturity))
                                                                : "-"))
                        % boost::io::group(left, setw(columnWidths[4]), getCoinStatus(c.m_status))
                        % boost::io::group(left, setw(columnWidths[5]), c.m_ID.m_Type)
                     << std::endl;
            }
            return true;
        });

        cout << std::endl;
    }

    void ShowAssetTxs(const IWalletDB::Ptr& walletDB, Asset::ID assetId, const char* coin, const char* groth)
    {
        auto txHistory = walletDB->getTxHistory(TxType::AssetReg);
        auto txIssue   = walletDB->getTxHistory(TxType::AssetIssue);
        auto txConsume = walletDB->getTxHistory(TxType::AssetConsume);
        auto txSimple  = walletDB->getTxHistory(TxType::Simple);
        auto txUnreg   = walletDB->getTxHistory(TxType::AssetUnreg);
        txHistory.insert(txHistory.end(), txIssue.begin(), txIssue.end());
        txHistory.insert(txHistory.end(), txConsume.begin(), txConsume.end());
        txHistory.insert(txHistory.end(), txSimple.begin(), txSimple.end());
        txHistory.insert(txHistory.end(), txUnreg.begin(), txUnreg.end());

        txHistory.erase(std::remove_if(txHistory.begin(), txHistory.end(), [&assetId](const auto& tx) {
            return tx.m_assetId != assetId;
        }), txHistory.end());

        if (txHistory.empty())
        {
            cout << kTxHistoryEmpty << endl;
            return;
        }

        if (!txHistory.empty())
        {
            const array<uint8_t, 6> columnWidths{{20, 17, 26, 21, 33, 65}};
                cout << boost::format(kTxHistoryTableHead)
                        % boost::io::group(left, setw(columnWidths[0]),  kTxHistoryColumnDatetTime)
                        % boost::io::group(left, setw(columnWidths[1]),  kTxHistoryColumnDirection)
                        % boost::io::group(right, setw(columnWidths[2]), kAssetTxHistoryColumnAmount)
                        % boost::io::group(left, setw(columnWidths[3]),  kTxHistoryColumnStatus)
                        % boost::io::group(left, setw(columnWidths[4]),  kTxHistoryColumnId)
                        % boost::io::group(left, setw(columnWidths[5]),  kTxHistoryColumnKernelId)
                     << std::endl;

            for (auto& tx : txHistory) {
                auto direction = tx.m_selfTx || tx.m_txType == TxType::AssetIssue || tx.m_txType == TxType::AssetConsume ||
                                 tx.m_txType == TxType::AssetReg || tx.m_txType == beam::wallet::TxType::AssetUnreg ?
                                 kTxDirectionSelf : (tx.m_sender ? kTxDirectionOut : kTxDirectionIn);
                cout << boost::format(kTxHistoryTableFormat)
                        % boost::io::group(left, setw(columnWidths[0]),  format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                        % boost::io::group(left, setw(columnWidths[1]),  direction)
                        % boost::io::group(right, setw(columnWidths[2]), to_string(PrintableAmount(tx.m_amount, true)))
                        % boost::io::group(left, setw(columnWidths[3]),  getTxStatus(tx))
                        % boost::io::group(left, setw(columnWidths[4]),  to_hex(tx.m_txId.data(), tx.m_txId.size()))
                        % boost::io::group(left, setw(columnWidths[5]),  to_string(tx.m_kernelID))
                     << std::endl;
            }
        }
    }

    int ShowWalletInfo(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        Block::SystemState::ID stateID = {};
        walletDB->getSystemStateID(stateID);
        storage::Totals totalsCalc(*walletDB);

        // Show info about BEAM
        const auto& totals = totalsCalc.GetTotals(Zero);
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
        ShowAssetCoins(walletDB, Zero, kBEAM, kGROTH);

        if (vm.count(cli::TX_HISTORY))
        {
            auto txHistory = walletDB->getTxHistory();
            txHistory.erase(std::remove_if(txHistory.begin(), txHistory.end(), [](const auto& tx) {
                return tx.m_assetId != 0;
            }), txHistory.end());

            if (txHistory.empty())
            {
            cout << kTxHistoryEmpty << endl;
            }
            else
            {
            const array<uint8_t, 6> columnWidths{ {20, 17, 26, 21, 33, 65} };
            cout << boost::format(kTxHistoryTableHead)
                % boost::io::group(left, setw(columnWidths[0]), kTxHistoryColumnDatetTime)
                % boost::io::group(left, setw(columnWidths[1]), kTxHistoryColumnDirection)
                % boost::io::group(right, setw(columnWidths[2]), kTxHistoryColumnAmount)
                % boost::io::group(left, setw(columnWidths[3]), kTxHistoryColumnStatus)
                % boost::io::group(left, setw(columnWidths[4]), kTxHistoryColumnId)
                % boost::io::group(left, setw(columnWidths[5]), kTxHistoryColumnKernelId)
                << std::endl;

            for (auto& tx : txHistory) {
                cout << boost::format(kTxHistoryTableFormat)
                    % boost::io::group(left, setw(columnWidths[0]),
                        format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                    % boost::io::group(left, setw(columnWidths[1]),
                    (tx.m_selfTx ? kTxDirectionSelf : (tx.m_sender ? kTxDirectionOut
                        : kTxDirectionIn)))
                    % boost::io::group(right, setw(columnWidths[2]),
                        to_string(PrintableAmount(tx.m_amount, true)))
                    % boost::io::group(left, setw(columnWidths[3]), getTxStatus(tx))
                    % boost::io::group(left, setw(columnWidths[4]), to_hex(tx.m_txId.data(), tx.m_txId.size()))
                    % boost::io::group(left, setw(columnWidths[5]), to_string(tx.m_kernelID))
                    << std::endl;
            }
            }
        }

        if (vm.count(cli::SWAP_TX_HISTORY))
        {
            auto txHistory = walletDB->getTxHistory(wallet::TxType::AtomicSwap);
            if (txHistory.empty())
            {
                cout << kSwapTxHistoryEmpty << endl;
            }
            else
            {
                const array<uint8_t, 6> columnWidths{ {20, 26, 18, 15, 23, 33} };
                cout << boost::format(kTxHistoryTableHead)
                    % boost::io::group(left, setw(columnWidths[0]), kTxHistoryColumnDatetTime)
                    % boost::io::group(right, setw(columnWidths[1]), kTxHistoryColumnAmount)
                    % boost::io::group(right, setw(columnWidths[2]), kTxHistoryColumnSwapAmount)
                    % boost::io::group(left, setw(columnWidths[3]), kTxHistoryColumnSwapType)
                    % boost::io::group(left, setw(columnWidths[4]), kTxHistoryColumnStatus)
                    % boost::io::group(left, setw(columnWidths[5]), kTxHistoryColumnId)
                    << std::endl;

                for (auto& tx : txHistory) {
                    Amount swapAmount = 0;
                    storage::getTxParameter(*walletDB, tx.m_txId, wallet::kDefaultSubTxID,
                        wallet::TxParameterID::AtomicSwapAmount, swapAmount);
                    bool isBeamSide = false;
                    storage::getTxParameter(*walletDB, tx.m_txId, wallet::kDefaultSubTxID,
                        wallet::TxParameterID::AtomicSwapIsBeamSide, isBeamSide);

                    AtomicSwapCoin swapCoin = AtomicSwapCoin::Unknown;
                    storage::getTxParameter(*walletDB, tx.m_txId, wallet::kDefaultSubTxID,
                        wallet::TxParameterID::AtomicSwapCoin, swapCoin);

                    stringstream ss;
                    ss << (isBeamSide ? kBEAM : to_string(swapCoin)) << " <--> "
                        << (!isBeamSide ? kBEAM : to_string(swapCoin));

                    cout << boost::format(kSwapTxHistoryTableFormat)
                        % boost::io::group(left, setw(columnWidths[0]),
                            format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                        % boost::io::group(right, setw(columnWidths[1]),
                            to_string(PrintableAmount(tx.m_amount, true)))
                        % boost::io::group(right, setw(columnWidths[2]), swapAmount)
                        % boost::io::group(right, setw(columnWidths[3]), ss.str())
                        % boost::io::group(left, setw(columnWidths[4]), getSwapTxStatus(walletDB, tx))
                        % boost::io::group(left, setw(columnWidths[5]), to_hex(tx.m_txId.data(), tx.m_txId.size()))
                        << std::endl;
                }
            }
        }

        //
        // Show info about assets
        //
        for (auto it : totalsCalc.allTotals) {
            const auto assetId = it.second.AssetId;
            if (assetId != 0) {
                cout << endl;
                ShowAssetInfo(it.second);
                ShowAssetCoins(walletDB, it.second.AssetId, kASSET, kAGROTH);
                if (vm.count(cli::TX_HISTORY))
                {
                    ShowAssetTxs(walletDB, it.second.AssetId, kASSET, kAGROTH);
                }
            }
        }

        return 0;
    }

    boost::optional<TxID> GetTxID(const po::variables_map& vm)
    {
        boost::optional<TxID> res;
        auto txIdStr = vm[cli::TX_ID].as<string>();
        if (txIdStr.empty())
        {
            LOG_ERROR() << kErrorTxIdParamReqired;
            return res;
        }
        
        auto txIdVec = from_hex(txIdStr);
        
        if (txIdVec.size() >= 16)
        {
            res.emplace();
            std::copy_n(txIdVec.begin(), 16, res->begin());
        }
        else
        {
            LOG_ERROR() << boost::format(kErrorTxIdParamInvalid) % txIdStr;
        }
        return res;
    }

    int TxDetails(const po::variables_map& vm)
    {
        auto txId = GetTxID(vm);
        if (!txId)
        {
            return -1;
        }
        auto walletDB = OpenDataBase(vm);
        auto tx = walletDB->getTx(*txId);
        if (!tx)
        {
            LOG_ERROR() << boost::format(kErrorTxWithIdNotFound) % vm[cli::TX_ID].as<string>();
            return -1;
        }

        LOG_INFO()
            << boost::format(kTxDetailsFormat)
                % storage::TxDetailsInfo(walletDB, *txId) % getTxStatus(*tx) 
            << (tx->m_status == TxStatus::Failed
                    ? boost::format(kTxDetailsFailReason) % GetFailureMessage(tx->m_failureReason)
                    : boost::format(""));

        return 0;
    }

    int ExportPaymentProof(const po::variables_map& vm)
    {
        auto txId = GetTxID(vm);
        if (!txId)
        {
            return -1;
        }

        auto walletDB = OpenDataBase(vm);
        auto tx = walletDB->getTx(*txId);
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

        auto res = storage::ExportPaymentProof(*walletDB, *txId);
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

        bool isValid = false;

        try
        {
            isValid = storage::VerifyPaymentProof(buf);
        }
        catch (const std::runtime_error & e)
        {
            LOG_ERROR() << e.what();
            throw std::runtime_error(kErrorPpInvalid);
        }

        if (!isValid)
            throw std::runtime_error(kErrorPpInvalid);

        return 0;
    }

    int ExportMinerKey(const po::variables_map& vm)
    {
        auto pass = GetPassword(vm);
        auto walletDB = OpenDataBase(vm, pass);

        uint32_t subKey = vm[cli::KEY_SUBKEY].as<Nonnegative<uint32_t>>().value;
        if (subKey < 1)
        {
            cout << kErrorSubkeyNotSpecified << endl;
            return -1;
        }

        Key::IKdf::Ptr pMaster = walletDB->get_MasterKdf();
        if (!pMaster)
        {
            cout << "Miner key not accessible" << endl;
            return -1;
        }

		Key::IKdf::Ptr pKey = MasterKey::get_Child(*pMaster, subKey);

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(subKey);

        ks.ExportS(*pKey);
        cout << boost::format(kSubKeyInfo) % subKey % ks.m_sRes << std::endl;

        return 0;
    }

    int ExportOwnerKey(const po::variables_map& vm)
    {
        auto pass = GetPassword(vm);
        auto walletDB = OpenDataBase(vm, pass);

        Key::IPKdf::Ptr pKey = walletDB->get_OwnerKdf();

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(0);

        ks.ExportP(*pKey);
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

    int ExportWalletData(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        auto s = storage::ExportDataToJson(*walletDB);
        return SaveExportedData(ByteBuffer(s.begin(), s.end()), vm[cli::IMPORT_EXPORT_PATH].as<string>()) ? 0 : -1;
    }

    int ImportWalletData(const po::variables_map& vm)
    {
        ByteBuffer buffer;
        if (vm[cli::IMPORT_EXPORT_PATH].defaulted())
        {
            LOG_ERROR() << kErrorFileLocationParamReqired;
            return -1;
        }

        auto path = vm[cli::IMPORT_EXPORT_PATH].as<string>();
        if (path.empty() || !LoadDataToImport(path, buffer))
        {
            LOG_ERROR() << boost::format(kErrorImportPathInvalid)
                        % path;
            return -1;
        }

        auto walletDB = OpenDataBase(vm);
        const char* p = (char*)(&buffer[0]);
        return storage::ImportDataFromJson(*walletDB, p, buffer.size()) ? 0 : -1;
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

    bool LoadReceiverParams(const po::variables_map& vm, TxParameters& params)
    {
        if (vm.find(cli::RECEIVER_ADDR) == vm.end())
        {
            LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }
        auto receiverParams = ParseParameters(vm[cli::RECEIVER_ADDR].as<string>());
        if (!receiverParams)
        {
            LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }
        return LoadReceiverParams(*receiverParams, params);
    }

    bool LoadBaseParamsForTX(const po::variables_map& vm, Asset::ID& assetId, Amount& amount, Amount& fee, WalletID& receiverWalletID, bool checkFee, bool skipReceiverWalletID=false)
    {
        if (!skipReceiverWalletID)
        {
            TxParameters params;
            if (!LoadReceiverParams(vm, params))
            {
                return false;
            }
            receiverWalletID = *params.GetParameter<WalletID>(TxParameterID::PeerID);
        }

        if (vm.count(cli::AMOUNT) == 0)
        {
            LOG_ERROR() << kErrorAmountMissing;
            return false;
        }

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

        if(vm.count(cli::ASSET_ID)) // asset id can be zero if beam only
        {
            assetId = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
        }

        return true;
    }

    template<typename Settings>
    bool ParseElectrumSettings(const po::variables_map& vm, Settings& settings)
    {
        if (vm.count(cli::ELECTRUM_SEED) || vm.count(cli::ELECTRUM_ADDR) ||
            vm.count(cli::GENERATE_ELECTRUM_SEED) || vm.count(cli::SELECT_SERVER_AUTOMATICALLY))
        {
            auto electrumSettings = settings.GetElectrumConnectionOptions();

            if (!electrumSettings.IsInitialized())
            {
                if (!vm.count(cli::ELECTRUM_SEED) && !vm.count(cli::GENERATE_ELECTRUM_SEED))
                {
                    throw std::runtime_error("electrum seed should be specified");
                }

                if (!vm.count(cli::ELECTRUM_ADDR) && 
                    (!vm.count(cli::SELECT_SERVER_AUTOMATICALLY) || (vm.count(cli::SELECT_SERVER_AUTOMATICALLY) && !vm[cli::SELECT_SERVER_AUTOMATICALLY].as<bool>())))
                {
                    throw std::runtime_error("electrum address should be specified");
                }
            }

            if (vm.count(cli::ELECTRUM_ADDR))
            {
                electrumSettings.m_address = vm[cli::ELECTRUM_ADDR].as<string>();
                if (!io::Address().resolve(electrumSettings.m_address.c_str()))
                {
                    throw std::runtime_error("unable to resolve electrum address: " + electrumSettings.m_address);
                }
            }

            if (vm.count(cli::SELECT_SERVER_AUTOMATICALLY))
            {
                electrumSettings.m_automaticChooseAddress = vm[cli::SELECT_SERVER_AUTOMATICALLY].as<bool>();
                if (!electrumSettings.m_automaticChooseAddress && electrumSettings.m_address.empty())
                {
                    throw std::runtime_error("electrum address should be specified");
                }
            }

            if (vm.count(cli::ELECTRUM_SEED))
            {
                auto tempPhrase = vm[cli::ELECTRUM_SEED].as<string>();
                boost::algorithm::trim_if(tempPhrase, [](char ch) { return ch == kElectrumSeparateSymbol; });
                electrumSettings.m_secretWords = string_helpers::split(tempPhrase, kElectrumSeparateSymbol);

                if (!bitcoin::validateElectrumMnemonic(electrumSettings.m_secretWords))
                {
                    if (bitcoin::validateElectrumMnemonic(electrumSettings.m_secretWords, true))
                    {
                        throw std::runtime_error("Segwit seed phrase is not supported yet.");
                    }
                    throw std::runtime_error("seed is not valid");
                }
            }
            else if (vm.count(cli::GENERATE_ELECTRUM_SEED))
            {
                electrumSettings.m_secretWords = bitcoin::createElectrumMnemonic(getEntropy());

                auto strSeed = std::accumulate(
                    std::next(electrumSettings.m_secretWords.begin()), electrumSettings.m_secretWords.end(), *electrumSettings.m_secretWords.begin(),
                    [](std::string a, std::string b)
                {
                    return a + kElectrumSeparateSymbol + b;
                });

                LOG_INFO() << "seed = " << strSeed;
            }

            settings.SetElectrumConnectionOptions(electrumSettings);

            return true;
        }

        return false;
    }

    template<typename Settings>
    bool ParseSwapSettings(const po::variables_map& vm, Settings& settings)
    {
        if (vm.count(cli::SWAP_WALLET_ADDR) > 0 || vm.count(cli::SWAP_WALLET_USER) > 0 || vm.count(cli::SWAP_WALLET_PASS) > 0)
        {
            auto coreSettings = settings.GetConnectionOptions();
            if (!coreSettings.IsInitialized())
            {
                if (vm.count(cli::SWAP_WALLET_USER) == 0)
                {
                    throw std::runtime_error(kErrorSwapWalletUserNameUnspecified);
                }

                if (vm.count(cli::SWAP_WALLET_ADDR) == 0)
                {
                    throw std::runtime_error(kErrorSwapWalletAddrUnspecified);
                }

                if (vm.count(cli::SWAP_WALLET_PASS) == 0)
                {
                    throw std::runtime_error(kErrorSwapWalletPwdNotProvided);
                }
            }

            if (vm.count(cli::SWAP_WALLET_USER))
            {
                coreSettings.m_userName = vm[cli::SWAP_WALLET_USER].as<string>();
            }

            if (vm.count(cli::SWAP_WALLET_ADDR))
            {
                string nodeUri = vm[cli::SWAP_WALLET_ADDR].as<string>();
                if (!coreSettings.m_address.resolve(nodeUri.c_str()))
                {
                    throw std::runtime_error((boost::format(kErrorSwapWalletAddrNotResolved) % nodeUri).str());
                }
            }

            // TODO roman.strilets: use SecString instead of std::string
            if (vm.count(cli::SWAP_WALLET_PASS))
            {
                coreSettings.m_pass = vm[cli::SWAP_WALLET_PASS].as<string>();
            }

            settings.SetConnectionOptions(coreSettings);

            return true;
        }

        return false;
    }

    template<typename SettingsProvider, typename Settings, typename CoreSettings, typename ElectrumSettings>
    int HandleSwapCoin(const po::variables_map& vm, const IWalletDB::Ptr& walletDB)
    {
        SettingsProvider settingsProvider{ walletDB };
        settingsProvider.Initialize();

        if (vm.count(cli::ALTCOIN_SETTINGS_RESET))
        {
            auto connectionType = bitcoin::from_string(vm[cli::ALTCOIN_SETTINGS_RESET].as<string>());

            if (connectionType)
            {
                if (*connectionType == bitcoin::ISettings::ConnectionType::Core)
                {
                    auto settings = settingsProvider.GetSettings();

                    if (!settings.GetElectrumConnectionOptions().IsInitialized())
                    {
                        settings = Settings{};
                    }
                    else
                    {
                        settings.SetConnectionOptions(CoreSettings{});

                        if (settings.GetCurrentConnectionType() == bitcoin::ISettings::ConnectionType::Core)
                        {
                            settings.ChangeConnectionType(bitcoin::ISettings::ConnectionType::Electrum);
                        }
                    }

                    settingsProvider.SetSettings(settings);
                    return 0;
                }

                if (*connectionType == bitcoin::ISettings::ConnectionType::Electrum)
                {
                    auto settings = settingsProvider.GetSettings();

                    if (!settings.GetConnectionOptions().IsInitialized())
                    {
                        settings = Settings{};
                    }
                    else
                    {
                        settings.SetElectrumConnectionOptions(ElectrumSettings{});

                        if (settings.GetCurrentConnectionType() == bitcoin::ISettings::ConnectionType::Electrum)
                        {
                            settings.ChangeConnectionType(bitcoin::ISettings::ConnectionType::Core);
                        }
                    }

                    settingsProvider.SetSettings(settings);
                    return 0;
                }
            }

            LOG_ERROR() << "unknown parameter";
            return -1;
        }

        auto settings = settingsProvider.GetSettings();
        bool isChanged = false;

        isChanged |= ParseSwapSettings(vm, settings);
        isChanged |= ParseElectrumSettings(vm, settings);

        if (!isChanged && !settings.IsInitialized())
        {
            LOG_INFO() << "settings should be specified.";
            return -1;
        }

        if (vm.count(cli::SWAP_FEERATE) == 0 && settings.GetFeeRate() == 0)
        {
            throw std::runtime_error(kErrorSwapFeeRateMissing);
        }

        if (vm.count(cli::SWAP_FEERATE))
        {
            settings.SetFeeRate(vm[cli::SWAP_FEERATE].as<Positive<Amount>>().value);
            isChanged = true;
        }

        if (vm.count(cli::ACTIVE_CONNECTION))
        {
            auto typeConnection = bitcoin::from_string(vm[cli::ACTIVE_CONNECTION].as<string>());
            if (!typeConnection)
            {
                throw std::runtime_error("active_connection is wrong");
            }

            if ((*typeConnection == bitcoin::ISettings::ConnectionType::Core && !settings.GetConnectionOptions().IsInitialized())
                || (*typeConnection == bitcoin::ISettings::ConnectionType::Electrum && !settings.GetElectrumConnectionOptions().IsInitialized()))
            {
                throw std::runtime_error(vm[cli::ACTIVE_CONNECTION].as<string>() + " is not initialized");
            }

            settings.ChangeConnectionType(*typeConnection);
            isChanged = true;
        }

        if (isChanged)
        {
            settingsProvider.SetSettings(settings);
        }
        return 0;
    }

    template<typename SettingsProvider>
    void ShowSwapSettings(const IWalletDB::Ptr& walletDB, const char* coinName)
    {
        SettingsProvider settingsProvider{ walletDB };
        
        settingsProvider.Initialize();
        auto settings = settingsProvider.GetSettings();

        if (settings.IsInitialized())
        {
            ostringstream stream;
            stream << "\n" << coinName << " settings" << '\n';
            if (settings.GetConnectionOptions().IsInitialized())
            {
                stream << "RPC user: " << settings.GetConnectionOptions().m_userName << '\n'
                    << "RPC node: " << settings.GetConnectionOptions().m_address.str() << '\n';
            }

            if (settings.GetElectrumConnectionOptions().IsInitialized())
            {
                if (settings.GetElectrumConnectionOptions().m_automaticChooseAddress)
                {
                    stream << "Automatic node selection mode is turned ON.\n";
                }
                else
                {
                    stream << "Electrum node: " << settings.GetElectrumConnectionOptions().m_address << '\n';
                }
            }
            stream << "Fee rate: " << settings.GetFeeRate() << '\n';
            stream << "Active connection: " << bitcoin::to_string(settings.GetCurrentConnectionType()) << '\n';

            LOG_INFO() << stream.str();
            return;
        }

        LOG_INFO() << coinName << " settings are not initialized.";
    }

    bool HasActiveSwapTx(const IWalletDB::Ptr& walletDB, AtomicSwapCoin swapCoin)
    {
        auto txHistory = walletDB->getTxHistory(wallet::TxType::AtomicSwap);

        for (const auto& tx : txHistory)
        {
            if (tx.m_status != TxStatus::Canceled && tx.m_status != TxStatus::Completed && tx.m_status != TxStatus::Failed)
            {
                AtomicSwapCoin txSwapCoin = AtomicSwapCoin::Unknown;
                storage::getTxParameter(*walletDB, tx.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::AtomicSwapCoin, txSwapCoin);
                if (txSwapCoin == swapCoin) return true;
            }
        }

        return false;
    }

    int SetSwapSettings(const po::variables_map& vm)
    {
        if (vm.count(cli::SWAP_COIN) > 0)
        {
            auto swapCoin = wallet::from_string(vm[cli::SWAP_COIN].as<string>());
            auto walletDB = OpenDataBase(vm);
            if (HasActiveSwapTx(walletDB, swapCoin))
            {
                LOG_ERROR() << "You cannot change settings while you have transactions in progress. Please wait untill transactions are completed and try again.";
                return -1;
            }

            switch (swapCoin)
            {
            case beam::wallet::AtomicSwapCoin::Bitcoin:
            {
                return HandleSwapCoin<bitcoin::SettingsProvider, bitcoin::Settings, bitcoin::BitcoinCoreSettings, bitcoin::ElectrumSettings>(vm, walletDB);
            }
            case beam::wallet::AtomicSwapCoin::Litecoin:
            {
                return HandleSwapCoin<litecoin::SettingsProvider, litecoin::Settings, litecoin::LitecoinCoreSettings, litecoin::ElectrumSettings>(vm, walletDB);
            }
            case beam::wallet::AtomicSwapCoin::Denarius:
            {
                return HandleSwapCoin<denarius::SettingsProvider, denarius::Settings, denarius::DenariusCoreSettings, denarius::ElectrumSettings>(vm, walletDB);
            }
            case beam::wallet::AtomicSwapCoin::Qtum:
            {
                return HandleSwapCoin<qtum::SettingsProvider, qtum::Settings, qtum::QtumCoreSettings, qtum::ElectrumSettings>(vm, walletDB);
            }
            default:
            {
                throw std::runtime_error("Unsupported coin for swap");
                break;
            }
            }
            return 0;
        }

        LOG_ERROR() << "swap_coin should be specified";
        return -1;
    }

    int ShowSwapSettings(const po::variables_map& vm)
    {
        if (vm.count(cli::SWAP_COIN) > 0)
        {
            auto walletDB = OpenDataBase(vm);
            auto swapCoin = wallet::from_string(vm[cli::SWAP_COIN].as<string>());
            switch (swapCoin)
            {
            case beam::wallet::AtomicSwapCoin::Bitcoin:
            {
                ShowSwapSettings<bitcoin::SettingsProvider>(walletDB, "bitcoin");
                break;
            }
            case beam::wallet::AtomicSwapCoin::Litecoin:
            {
                ShowSwapSettings<litecoin::SettingsProvider>(walletDB, "litecoin");
                break;
            }
            case beam::wallet::AtomicSwapCoin::Denarius:
            {
                ShowSwapSettings<denarius::SettingsProvider>(walletDB, "denarius");
                break;
            }
            case beam::wallet::AtomicSwapCoin::Qtum:
            {
                ShowSwapSettings<qtum::SettingsProvider>(walletDB, "qtum");
                break;
            }
            default:
            {
                throw std::runtime_error("Unsupported coin for swap");
                break;
            }
            }
            return 0;
        }

        LOG_ERROR() << "swap_coin should be specified";
        return -1;
    }

    boost::optional<TxID> InitSwap(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, Wallet& wallet, bool checkFee)
    {
        if (vm.count(cli::SWAP_AMOUNT) == 0)
        {
            throw std::runtime_error(kErrorSwapAmountMissing);
        }

        Amount swapAmount = vm[cli::SWAP_AMOUNT].as<Positive<Amount>>().value;
        wallet::AtomicSwapCoin swapCoin = wallet::AtomicSwapCoin::Bitcoin;

        if (vm.count(cli::SWAP_COIN) > 0)
        {
            swapCoin = wallet::from_string(vm[cli::SWAP_COIN].as<string>());
        }

        Amount swapFeeRate = GetSwapFeeRate(walletDB, swapCoin);
        bool isSwapAmountValid =
            IsSwapAmountValid(swapCoin, swapAmount, swapFeeRate);
        if (!isSwapAmountValid)
            throw std::runtime_error("The swap amount must be greater than the redemption fee.");

        bool isBeamSide = (vm.count(cli::SWAP_BEAM_SIDE) != 0);

        Asset::ID assetId = Asset::s_InvalidID;
        Amount amount = 0;
        Amount fee = 0;
        WalletID receiverWalletID(Zero);

        if (!LoadBaseParamsForTX(vm, assetId, amount, fee, receiverWalletID, checkFee, true))
        {
            return boost::none;
        }

        if (assetId)
        {
            throw std::runtime_error(kErrorCantSwapAsset);
        }

        if (vm.count(cli::SWAP_AMOUNT) == 0)
        {
            throw std::runtime_error(kErrorSwapAmountMissing);
        }

        if (amount <= kMinFeeInGroth)
        {
            throw std::runtime_error(kErrorSwapAmountTooLow);
        }

        WalletAddress senderAddress = GenerateNewAddress(walletDB, "");

        // TODO:SWAP use async callbacks or IWalletObserver?

        Height minHeight = walletDB->getCurrentHeight();
        auto swapTxParameters = CreateSwapTransactionParameters();
        FillSwapTxParams(&swapTxParameters,
                         senderAddress.m_walletID,
                         minHeight,
                         amount,
                         fee,
                         swapCoin,
                         swapAmount,
                         swapFeeRate,
                         isBeamSide);

        boost::optional<TxID> currentTxID = wallet.StartTransaction(swapTxParameters);

        // print swap tx token
        {
            const auto& mirroredTxParams = MirrorSwapTxParams(swapTxParameters);
            const auto& readyForTokenizeTxParams =
                PrepareSwapTxParamsForTokenization(mirroredTxParams);
            auto swapTxToken = std::to_string(readyForTokenizeTxParams);
            LOG_INFO() << "Swap token: " << swapTxToken;
        }
        return currentTxID;
    }

    boost::optional<TxID> AcceptSwap(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, Wallet& wallet, bool checkFee)
    {
        if (vm.count(cli::SWAP_TOKEN) == 0)
        {
            throw std::runtime_error("swap transaction token should be specified");
        }

        auto swapTxToken = vm[cli::SWAP_TOKEN].as<std::string>();
        auto swapTxParameters = beam::wallet::ParseParameters(swapTxToken);

        // validate TxType and parameters
        auto transactionType = swapTxParameters->GetParameter<TxType>(TxParameterID::TransactionType);
        auto isBeamSide = swapTxParameters->GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
        auto swapCoin = swapTxParameters->GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        auto beamAmount = swapTxParameters->GetParameter<Amount>(TxParameterID::Amount);
        auto swapAmount = swapTxParameters->GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
        auto peerID = swapTxParameters->GetParameter<WalletID>(TxParameterID::PeerID);
        auto peerResponseTime = swapTxParameters->GetParameter<Height>(TxParameterID::PeerResponseTime);
        auto createTime = swapTxParameters->GetParameter<Height>(TxParameterID::CreateTime);
        auto minHeight = swapTxParameters->GetParameter<Height>(TxParameterID::MinHeight);

        bool isValidToken = isBeamSide && swapCoin && beamAmount && swapAmount && peerID && peerResponseTime && createTime && minHeight;

        if (!transactionType || *transactionType != TxType::AtomicSwap || !isValidToken)
        {
            throw std::runtime_error("swap transaction token is invalid.");
        }

        Amount swapFeeRate = 0;

        if (swapCoin == wallet::AtomicSwapCoin::Bitcoin)
        {
            auto btcSettingsProvider = std::make_shared<bitcoin::SettingsProvider>(walletDB);
            btcSettingsProvider->Initialize();
            auto btcSettings = btcSettingsProvider->GetSettings();
            if (!btcSettings.IsInitialized())
            {
                throw std::runtime_error("BTC settings should be initialized.");
            }

            if (!BitcoinSide::CheckAmount(*swapAmount, btcSettings.GetFeeRate()))
            {
                throw std::runtime_error("The swap amount must be greater than the redemption fee.");
            }
            swapFeeRate = btcSettings.GetFeeRate();
        }
        else if (swapCoin == wallet::AtomicSwapCoin::Litecoin)
        {
            auto ltcSettingsProvider = std::make_shared<litecoin::SettingsProvider>(walletDB);
            ltcSettingsProvider->Initialize();
            auto ltcSettings = ltcSettingsProvider->GetSettings();
            if (!ltcSettings.IsInitialized())
            {
                throw std::runtime_error("LTC settings should be initialized.");
            }

            if (!LitecoinSide::CheckAmount(*swapAmount, ltcSettings.GetFeeRate()))
            {
                throw std::runtime_error("The swap amount must be greater than the redemption fee.");
            }
            swapFeeRate = ltcSettings.GetFeeRate();
        }
        else if (swapCoin == wallet::AtomicSwapCoin::Denarius)
        {
            auto dSettingsProvider = std::make_shared<denarius::SettingsProvider>(walletDB);
            dSettingsProvider->Initialize();
            auto dSettings = dSettingsProvider->GetSettings();
            if (!dSettings.IsInitialized())
            {
                throw std::runtime_error("D settings should be initialized.");
            }

            if (!DenariusSide::CheckAmount(*swapAmount, dSettings.GetFeeRate()))
            {
                throw std::runtime_error("The swap amount must be greater than the redemption fee.");
            }
            swapFeeRate = dSettings.GetFeeRate();
        }
        else if (swapCoin == wallet::AtomicSwapCoin::Qtum)
        {
            auto qtumSettingsProvider = std::make_shared<qtum::SettingsProvider>(walletDB);
            qtumSettingsProvider->Initialize();
            auto qtumSettings = qtumSettingsProvider->GetSettings();
            if (!qtumSettings.IsInitialized())
            {
                throw std::runtime_error("Qtum settings should be initialized.");
            }

            if (!QtumSide::CheckAmount(*swapAmount, qtumSettings.GetFeeRate()))
            {
                throw std::runtime_error("The swap amount must be greater than the redemption fee.");
            }
            swapFeeRate = qtumSettings.GetFeeRate();
        }
        else
        {
            throw std::runtime_error("Unsupported swap coin.");
        }

        // display swap details to user
        cout << " Swap conditions: " << "\n"
            << " Beam side:    " << *isBeamSide << "\n"
            << " Swap coin:    " << to_string(*swapCoin) << "\n"
            << " Beam amount:  " << PrintableAmount(*beamAmount) << "\n"
            << " Swap amount:  " << *swapAmount << "\n"
            << " Peer ID:      " << to_string(*peerID) << "\n";

        // get accepting
        // TODO: Refactor
        bool isAccepted = false;
        while (true)
        {
            std::string result;
            cout << "Do you agree to these conditions? (y/n): ";
            cin >> result;

            if (result == "y" || result == "n")
            {
                isAccepted = (result == "y");
                break;
            }
        }

        if (!isAccepted)
        {
            LOG_INFO() << "Swap rejected!";
            return boost::none;
        }

        // on accepting
        WalletAddress senderAddress = GenerateNewAddress(walletDB, "");

        Amount fee = cli::kMinimumFee;
        swapTxParameters->SetParameter(TxParameterID::MyID, senderAddress.m_walletID);
        FillSwapFee(&(*swapTxParameters), fee, swapFeeRate, *isBeamSide);

        return wallet.StartTransaction(*swapTxParameters);
    }

    struct CliNodeConnection final : public proto::FlyClient::NetworkStd
    {
    public:
        CliNodeConnection(proto::FlyClient& fc) : proto::FlyClient::NetworkStd(fc) {};
        void OnConnectionFailed(const proto::NodeConnection::DisconnectReason& reason) override
        {
            LOG_ERROR() << kErrorConnectionFailed;
        };
    };

    proto::FlyClient::NetworkStd::Ptr CreateNetwork(
        proto::FlyClient& fc, const po::variables_map& vm)
    {
        if (vm.count(cli::NODE_ADDR) == 0)
        {
            LOG_ERROR() << kErrorNodeAddrNotSpecified;
            return nullptr;
        }

        string nodeURI = vm[cli::NODE_ADDR].as<string>();
        io::Address nodeAddress;
        if (!nodeAddress.resolve(nodeURI.c_str()))
        {
            LOG_ERROR() << boost::format(kErrorNodeAddrUnresolved) % nodeURI;
            return nullptr;
        }

        auto nnet = make_shared<CliNodeConnection>(fc);
        nnet->m_Cfg.m_PollPeriod_ms =
            vm[cli::NODE_POLL_PERIOD].as<Nonnegative<uint32_t>>().value;
        if (nnet->m_Cfg.m_PollPeriod_ms)
        {
            LOG_INFO() << boost::format(kNodePoolPeriod)
                       % nnet->m_Cfg.m_PollPeriod_ms;
            uint32_t timeout_ms =
                std::max(Rules::get().DA.Target_s * 1000,
                         nnet->m_Cfg.m_PollPeriod_ms);
            if (timeout_ms != nnet->m_Cfg.m_PollPeriod_ms)
            {
                LOG_INFO() << boost::format(kNodePoolPeriodRounded)
                           % timeout_ms;
            }
        }
        uint32_t responceTime_s =
            Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
        if (nnet->m_Cfg.m_PollPeriod_ms >= responceTime_s * 1000)
        {
            LOG_WARNING() << boost::format(kErrorNodePoolPeriodTooMuch)
                          % uint32_t(responceTime_s / 3600);
        }
        nnet->m_Cfg.m_vNodes.push_back(nodeAddress);
        nnet->m_Cfg.m_UseProxy = vm[cli::PROXY_USE].as<bool>();
        if (nnet->m_Cfg.m_UseProxy)
        {
            string proxyURI = vm[cli::PROXY_ADDRESS].as<string>();
            io::Address proxyAddr;
            if (!proxyAddr.resolve(proxyURI.c_str()))
            {
                LOG_ERROR() << boost::format(kErrorNodeAddrUnresolved) % proxyURI;
                return nullptr;
            }
            nnet->m_Cfg.m_ProxyAddr = proxyAddr;
        }
        nnet->Connect();

        return nnet;
    }

    TxID IssueConsumeAsset(bool issue, const po::variables_map& vm, Wallet& wallet)
    {
        if(!vm.count(cli::ASSET_ID)) // asset id can be zero if beam only
        {
            throw std::runtime_error(kErrorAssetIdRequired);
        }
        Asset::ID aid = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;

        if(!vm.count(cli::ASSET_INDEX))
        {
            throw std::runtime_error(kErrorAssetIdxRequired);
        }
        const Key::Index aidx = vm[cli::ASSET_INDEX].as<Positive<uint32_t>>().value;

        if (!vm.count(cli::AMOUNT))
        {
            throw std::runtime_error(kErrorAmountMissing);
        }
        double cliAmount = vm[cli::AMOUNT].as<Positive<double>>().value;
        Amount amountGroth = static_cast<ECC::Amount>(std::round(cliAmount * Rules::Coin));
        if (amountGroth == 0) /// TODO:ASSETS - check if necessary, may be Positive<> above would throw
        {
            throw std::runtime_error(kErrorZeroAmount);
        }

        auto fee = vm[cli::FEE].as<Nonnegative<Amount>>().value;
        if (fee < cli::kMinimumFee)
        {
            throw std::runtime_error(kErrorFeeToLow);
        }

        auto params = CreateTransactionParameters(issue ? TxType::AssetIssue : TxType::AssetConsume)
                        .SetParameter(TxParameterID::Amount, amountGroth)
                        .SetParameter(TxParameterID::Fee, fee)
                        .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm))
                        .SetParameter(TxParameterID::AssetOwnerIdx, aidx)
                        .SetParameter(TxParameterID::AssetID, aid);

        return wallet.StartTransaction(params);
    }

    TxID RegUnregAsset(bool reg, const po::variables_map& vm, Wallet& wallet)
    {
        if(!vm.count(cli::ASSET_INDEX))
        {
            throw std::runtime_error(kErrorAssetIdxRequired);
        }

        const Key::Index aidx = vm[cli::ASSET_INDEX].as<Positive<uint32_t>>().value;

        auto fee = vm[cli::FEE].as<Nonnegative<Amount>>().value;
        if (fee < cli::kMinimumFee)
        {
            LOG_ERROR() << "Test: " << kErrorFeeToLow;
            throw std::runtime_error(kErrorFeeToLow);
        }

        auto params = CreateTransactionParameters(reg ? TxType::AssetReg : TxType::AssetUnreg)
                        .SetParameter(TxParameterID::Amount, Rules::get().CA.DepositForList)
                        .SetParameter(TxParameterID::Fee, fee)
                        .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm))
                        .SetParameter(TxParameterID::AssetOwnerIdx, aidx);

        if (reg)
        {
            if(!vm.count(cli::METADATA))
            {
                throw std::runtime_error(kErrorAssetMetadataRequired);
            }

            std::string meta = vm[cli::METADATA].as<std::string>();
            if (meta.empty())
            {
                throw std::runtime_error(kErrorAssetMetadataRequired);
            }

            params.SetParameter(TxParameterID::AssetMetadata, meta);
        }

        return wallet.StartTransaction(params);
    }

    TxID GetAssetInfo(const po::variables_map& vm, Wallet& wallet)
    {
        if(!vm.count(cli::ASSET_ID)) // asset id can be zero if beam only
        {
            throw std::runtime_error(kErrorAssetIdRequired);
        }

        Asset::ID aid = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
        auto params = CreateTransactionParameters(TxType::AssetInfo)
                        .SetParameter(TxParameterID::AssetID, aid);

        return wallet.StartTransaction(params);
    }

#ifdef BEAM_LASER_SUPPORT

    int HandleLaser(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        auto laser =
            std::make_unique<laser::Mediator>(walletDB);

        if (vm.count(cli::LASER_LIST))
        {
            LaserShow(walletDB);
            return 0;
        }

        if (vm.count(cli::LASER_DELETE))
        {
            if (LaserDelete(laser, vm))
            {
                LaserShow(walletDB);
                return 0;
            }
            return -1;
        }

        auto nnet = CreateNetwork(*laser, vm);
        if (!nnet)
        {
            return -1;
        }
        laser->SetNetwork(nnet);

        LaserObserver laserObserver(walletDB, vm);
        laser->AddObserver(&laserObserver);

        if (ProcessLaser(laser, walletDB, vm))
        {
            io::Reactor::get_Current().run();
            return 0;
        }

        return -1;
    }

#endif  // BEAM_LASER_SUPPORT

    int DoWalletFunc(const po::variables_map& vm, std::function<int (const po::variables_map&, Wallet&, IWalletDB::Ptr, boost::optional<TxID>&, bool)> func)
    {
        LOG_INFO() << kStartMessage;
        auto walletDB = OpenDataBase(vm);

        const auto& currHeight = walletDB->getCurrentHeight();
        const auto& fork1Height = Rules::get().pForks[1].m_Height;
        const bool isFork1 = currHeight >= fork1Height;

        bool isServer = vm[cli::COMMAND].as<string>() == cli::LISTEN || vm.count(cli::LISTEN);

        boost::optional<TxID> currentTxID;
        auto onTxCompleteAction = [&currentTxID](const TxID& txID)
        {
            if (currentTxID.is_initialized() &&
                currentTxID.get() != txID)
            {
                return;
            }
            io::Reactor::get_Current().stop();
        };

        auto txCompletedAction = isServer
            ? Wallet::TxCompletedAction()
            : onTxCompleteAction;

        Wallet wallet{ walletDB,
                       std::move(txCompletedAction),
                       Wallet::UpdateCompletedAction() };
        {
            wallet::AsyncContextHolder holder(wallet);

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
            RegisterSwapTxCreators(wallet, walletDB);
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
#ifdef BEAM_CONFIDENTIAL_ASSETS_SUPPORT
            RegisterAssetCreators(wallet);
#endif  // BEAM_CONFIDENTIAL_ASSETS_SUPPORT
            wallet.ResumeAllTransactions();

            auto nnet = CreateNetwork(wallet, vm);
            if (!nnet)
            {
                return -1;
            }
            wallet.AddMessageEndpoint(make_shared<WalletNetworkViaBbs>(wallet, nnet, walletDB));
            wallet.SetNodeEndpoint(nnet);

            int res = func(vm, wallet, walletDB, currentTxID, isFork1);
            if (res != 0)
            {
                return res;
            }
        }
        io::Reactor::get_Current().run();
        return 0;
    }

    int Send(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                io::Address receiverAddr;
                Asset::ID assetId = Asset::s_InvalidID;
                Amount amount = 0;
                Amount fee = 0;
                WalletID receiverWalletID(Zero);

                if (!LoadBaseParamsForTX(vm, assetId, amount, fee, receiverWalletID, isFork1))
                {
                    return -1;
                }
                WalletAddress senderAddress = GenerateNewAddress(walletDB, "");
                auto params = CreateSimpleTransactionParameters();
                LoadReceiverParams(vm, params);
                params.SetParameter(TxParameterID::MyID, senderAddress.m_walletID)
                    .SetParameter(TxParameterID::Amount, amount)
                    .SetParameter(TxParameterID::Fee, fee)
                    .SetParameter(TxParameterID::AssetID, assetId)
                    .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm));
                currentTxID = wallet.StartTransaction(params);

                return 0;
            });
    }

    int Listen(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                return 0;
            });
    }
    
    int Rescan(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                wallet.Rescan();
                return 0;
            });
    }

    int CreateNewAddress(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        if (!CreateNewAddress(vm, walletDB))
        {
            return -1;
        }

        if (!vm.count(cli::LISTEN))
        {
            return 0;
        }

        return Listen(vm);
    }

    int DeleteTransaction(const po::variables_map& vm)
    {
        auto txId = GetTxID(vm);
        if (!txId)
        {
            return -1;
        }

        auto walletDB = OpenDataBase(vm);
        auto tx = walletDB->getTx(*txId);
        if (tx)
        {
            LOG_INFO() << "deleting tx " << *txId;
            if (tx->canDelete())
            {
                walletDB->deleteTx(*txId);
                return 0;
            }
            LOG_ERROR() << kErrorTxStatusInvalid;
            return -1;
        }

        LOG_ERROR() << kErrorTxIdUnknown;
        return -1;
    }

    int CancelTransaction(const po::variables_map& vm)
    {
        auto txId = GetTxID(vm);
        if (!txId)
        {
            return -1;
        }

        return DoWalletFunc(vm, [&txId](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                auto tx = walletDB->getTx(*txId);
                if (tx)
                {
                    if (wallet.CanCancelTransaction(*txId))
                    {
                        currentTxID = *txId;
                        wallet.CancelTransaction(*txId);
                        return 0;
                    }
                    
                    LOG_ERROR() << kErrorCancelTxInInvalidStatus << (tx->m_txType == wallet::TxType::AtomicSwap ? beam::getSwapTxStatus(walletDB, *tx) : beam::getTxStatus(*tx));
                    return -1;
                }
                LOG_ERROR() << kErrorTxIdUnknown;
                return -1;
            });
    }

    int InitSwap(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                if (!wallet.IsWalletInSync())
                {
                    return -1;
                }

                currentTxID = InitSwap(vm, walletDB, wallet, isFork1);
                if (!currentTxID)
                {
                    return -1;
                }

                return 0;
            });
    }

    int AcceptSwap(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = AcceptSwap(vm, walletDB, wallet, isFork1);
                if (!currentTxID)
                {
                    return -1;
                }
                return 0;
            });
    }

    int IssueAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = IssueConsumeAsset(true, vm, wallet);
                return 0;
            });
    }

    int ConsumeAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = IssueConsumeAsset(false, vm, wallet);
                return 0;
            });
    }

    int RegisterAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = RegUnregAsset(true, vm, wallet);
                return 0;
            });
    }

    int UnregisterAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = RegUnregAsset(false, vm, wallet);
                return 0;
            });
    }

    int ShowAssetInfo(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = GetAssetInfo(vm, wallet);
                return 0;
            });
    }
}  // namespace

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

                if (vm.count(cli::COMMAND) == 0)
                {
                    LOG_ERROR() << kErrorCommandNotSpecified;
                    printHelp(visibleOptions);
                    return 0;
                }

                auto command = vm[cli::COMMAND].as<string>();

                using CommandFunc = int (*)(const po::variables_map&);
                const std::pair<string, CommandFunc> commands[] =
                {
                    {cli::INIT,               InitWallet},
                    {cli::RESTORE,            RestoreWallet},
                    {cli::SEND,               Send},
                    {cli::LISTEN,             Listen},
                    {cli::TREASURY,           HandleTreasury},
                    {cli::INFO,               ShowWalletInfo},
                    {cli::EXPORT_MINER_KEY,   ExportMinerKey},
                    {cli::EXPORT_OWNER_KEY,   ExportOwnerKey},
                    {cli::NEW_ADDRESS,        CreateNewAddress},
                    {cli::CANCEL_TX,          CancelTransaction},
                    {cli::DELETE_TX,          DeleteTransaction},
                    {cli::CHANGE_ADDRESS_EXPIRATION, ChangeAddressExpiration},
                    {cli::TX_DETAILS,         TxDetails},
                    {cli::PAYMENT_PROOF_EXPORT, ExportPaymentProof},
                    {cli::PAYMENT_PROOF_VERIFY, VerifyPaymentProof},
                    {cli::GENERATE_PHRASE,      GeneratePhrase},
                    {cli::WALLET_ADDRESS_LIST,  ShowAddressList},
                    {cli::WALLET_RESCAN,        Rescan},
                    {cli::IMPORT_DATA,          ImportWalletData},
                    {cli::EXPORT_DATA,          ExportWalletData},
                    {cli::SWAP_INIT,            InitSwap},
                    {cli::SWAP_ACCEPT,          AcceptSwap},
                    {cli::SET_SWAP_SETTINGS,    SetSwapSettings},
                    {cli::SHOW_SWAP_SETTINGS,   ShowSwapSettings},
                    {cli::GET_TOKEN,            GetToken},
#ifdef BEAM_LASER_SUPPORT   
                    {cli::LASER,                HandleLaser},
#endif  // BEAM_LASER_SUPPORT
                    {cli::ASSET_ISSUE,          IssueAsset},
                    {cli::ASSET_CONSUME,        ConsumeAsset},
                    {cli::ASSET_REGISTER,       RegisterAsset},
                    {cli::ASSET_UNREGISTER,     UnregisterAsset},
                    {cli::ASSET_INFO,           ShowAssetInfo},
                };

                auto cit = find_if(begin(commands), end(commands), [&command](auto& p) {return p.first == command; });
                if (cit == end(commands))
                {
                    LOG_ERROR() << boost::format(kErrorCommandUnknown) % command;
                    return -1;
                }

                LOG_INFO() << boost::format(kVersionInfo) % PROJECT_VERSION % BRANCH_NAME;
                LOG_INFO() << boost::format(kRulesSignatureInfo) % Rules::get().get_SignatureStr();
                        
                return cit->second(vm);
            }
        }
        catch (const AddressExpiredException&)
        {
        }
        catch (const FailToStartSwapException&)
        {
        }
        catch (const FileIsNotDatabaseException&)
        {
            LOG_ERROR() << kErrorCantOpenWallet;
            return -1;
        }
        catch (const DatabaseException & ex)
        {
            LOG_ERROR() << ex.what();
            return -1;
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
