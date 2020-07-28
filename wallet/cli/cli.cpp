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
#include "wallet/transactions/swaps/bridges/qtum/electrum.h"
#include "wallet/transactions/swaps/bridges/litecoin/litecoin.h"
#include "wallet/transactions/swaps/bridges/qtum/qtum.h"
#include "wallet/transactions/swaps/bridges/bitcoin_cash/bitcoin_cash.h"
#include "wallet/transactions/swaps/bridges/bitcoin_sv/bitcoin_sv.h"
#include "wallet/transactions/swaps/bridges/dogecoin/dogecoin.h"
#include "wallet/transactions/swaps/bridges/dash/dash.h"

#include "wallet/transactions/swaps/common.h"
#include "wallet/transactions/swaps/utils.h"
#include "wallet/transactions/assets/assets_reg_creators.h"
#include "keykeeper/local_private_key_keeper.h"
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "core/treasury.h"
#include "core/block_rw.h"
#include <algorithm>
//#include "unittests/util.h"
#include "mnemonic/mnemonic.h"
#include "utility/string_helpers.h"
#include "version.h"

//lelantus
#include "wallet/transactions/lelantus/pull_transaction.h"
#include "wallet/transactions/lelantus/push_transaction.h"
#include "wallet/transactions/lelantus/lelantus_reg_creators.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include "utility/cli/options.h"
#include "utility/log_rotation.h"
#include "utility/helpers.h"
#include "wallet/core/assets_utils.h"

#ifdef BEAM_LASER_SUPPORT
#include "laser.h"
#include "wallet/laser/mediator.h"
#endif  // BEAM_LASER_SUPPORT

#include <boost/assert.hpp> 
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

#include <iomanip>
#include <iterator>
#include <future>
#include <regex>
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
        walletDB->addStatusInterpreterCreator(TxType::Simple, [] (const TxParameters& txParams) {
            return TxStatusInterpreter(txParams);
        });

        auto assetsStatusIterpreterCreator = [] (const TxParameters& txParams)
        {
            return AssetTxStatusInterpreter(txParams);
        };

        walletDB->addStatusInterpreterCreator(TxType::AssetIssue, assetsStatusIterpreterCreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetConsume, assetsStatusIterpreterCreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetReg, assetsStatusIterpreterCreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetUnreg, assetsStatusIterpreterCreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetInfo, assetsStatusIterpreterCreator);

        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
        walletDB->addStatusInterpreterCreator(TxType::AtomicSwap, [] (const TxParameters& txParams) {
            struct CliSwapTxStatusInterpreter : public TxStatusInterpreter
            {
                explicit CliSwapTxStatusInterpreter(const TxParameters& txParams) : TxStatusInterpreter(txParams)
                {
                    auto value = txParams.GetParameter(wallet::TxParameterID::State);
                    if (value) fromByteBuffer(*value, m_state);
                }
                virtual ~CliSwapTxStatusInterpreter() {}
                std::string getStatus() const override
                {
                    return wallet::getSwapTxStatus(m_state);
                }
                wallet::AtomicSwapTransaction::State m_state = wallet::AtomicSwapTransaction::State::Initial;

            };
            return CliSwapTxStatusInterpreter(txParams);
        });
        #endif

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

    using CommandFunc = int (*)(const po::variables_map&);
    struct Command
    {
        std::string name;
        CommandFunc handler;
        std::string description;
    };

    // simple formating
    void PrintParagraph(std::stringstream& ss, const std::string& text, size_t start, size_t end)
    {
        for (size_t s = ss.str().size(); s < start; ++s)
        {
            ss.put(' ');
        }
        size_t textPos = 0, linePos = start;
        bool skipSpaces = true;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (isspace(text[i]))
            {
                if(skipSpaces)
                {
                    for (; isspace(text[textPos]) && textPos != i; ++textPos);
                    skipSpaces = false;
                }

                for (; textPos != i; ++textPos)
                {
                    ss.put(text[textPos]);
                }
            }

            if (linePos == end)
            {
                ss.put('\n');
                {
                    size_t j = start;
                    while (j--)
                    {
                        ss.put(' ');
                    }
                }
                linePos = start;
                skipSpaces = true;
            }
            else
            {
                ++linePos;
            }
        }

        if (skipSpaces)
        {
            for (; isspace(text[textPos]) && textPos != text.size(); ++textPos);
            skipSpaces = false;
        }

        for (; textPos != text.size(); ++textPos)
        {
            ss.put(text[textPos]);
        }
    }

    void printHelp(const Command* begin, const Command* end, const po::options_description& options)
    {
        if (begin != end)
        {
            cout << "\nUSAGE: " << APP_NAME << " <command> [options] [configuration rules]\n\n";
            cout << "COMMANDS:\n";
            for (auto it = begin; it != end; ++it)
            {
                std::stringstream ss;
                ss << "  " << it->name;
                PrintParagraph(ss, it->description, 40, 80);
                ss << '\n';
                cout << ss.str();
            }
            cout << std::endl;
        }
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

    void AddVoucherParameter(const po::variables_map& vm, TxParameters& params, IWalletDB::Ptr db, uint64_t ownID)
    {
        if (auto it = vm.find(cli::VOUCHER_COUNT); it != vm.end())
        {
            auto kdf = db->get_MasterKdf();
            auto vouchers = GenerateVoucherList(kdf, ownID, it->second.as<Positive<uint32_t>>().value);
            if (!vouchers.empty())
            {
                // add voucher parameter
                params.SetParameter(TxParameterID::ShieldedVoucherList, vouchers);
                params.SetParameter(TxParameterID::TransactionType, beam::wallet::TxType::PushTransaction);
            }
        }
    }

    int GetToken(const po::variables_map& vm)
    {
        TxParameters params;
        if (auto it = vm.find(cli::RECEIVER_ADDR); it != vm.end())
        {
            auto receiver = it->second.as<string>();
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
#ifdef BEAM_LIB_VERSION
            params.SetParameter(beam::wallet::TxParameterID::LibraryVersion, std::string(BEAM_LIB_VERSION));
#endif // BEAM_LIB_VERSION
            params.SetParameter(TxParameterID::PeerWalletIdentity, address->m_Identity);
            AddVoucherParameter(vm, params, walletDB, address->m_OwnID);
        }
        else
        {
            auto walletDB = OpenDataBase(vm);
            WalletAddress address = GenerateNewAddress(walletDB, "");
            
            params.SetParameter(TxParameterID::PeerID, address.m_walletID);
            params.SetParameter(TxParameterID::PeerWalletIdentity, address.m_Identity);
            AddVoucherParameter(vm, params, walletDB, address.m_OwnID);
        }

        if (!params.GetParameter(TxParameterID::TransactionType))
        {
            params.SetParameter(TxParameterID::TransactionType, beam::wallet::TxType::Simple);
        }
        LOG_INFO() << "token: " << to_string(params);
        return 0;
    }

    int SetConfirmationsCount(const po::variables_map& vm)
    {
        uint32_t confirmations_count = vm[cli::CONFIRMATIONS_COUNT].as<Nonnegative<uint32_t>>().value;
        auto walletDB = OpenDataBase(vm);
        walletDB->setCoinConfirmationsOffset(confirmations_count);
        cout << boost::format(kCoinConfirmationsCount) % confirmations_count << std::endl;
        return 0;
    }

    int GetConfirmationsCount(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        auto confirmations_count = walletDB->getCoinConfirmationsOffset();
        cout << boost::format(kCoinConfirmationsCount) % confirmations_count << std::endl;
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
        array<uint8_t, 6> columnWidths{ { 20, 70, 70, 8, 20, 21 } };

        // Comment | Address | Identity | Active | Expiration date | Created |
        cout << boost::format(kAddrListTableHead)
             % boost::io::group(left, setw(columnWidths[0]), kAddrListColumnComment)
             % boost::io::group(left, setw(columnWidths[1]), kAddrListColumnAddress)
             % boost::io::group(left, setw(columnWidths[2]), kAddrListColumnIdentity)
             % boost::io::group(left, setw(columnWidths[3]), kAddrListColumnActive)
             % boost::io::group(left, setw(columnWidths[4]), kAddrListColumnExprDate)
             % boost::io::group(left, setw(columnWidths[5]), kAddrListColumnCreated)
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
             % boost::io::group(left, setw(columnWidths[2]), std::to_string(address.m_Identity))
             % boost::io::group(left, boolalpha, setw(columnWidths[3]), !address.isExpired())
             % boost::io::group(left, setw(columnWidths[4]), expirationDateText)
             % boost::io::group(left, setw(columnWidths[5]), creationDateText)
             << std::endl;
        }

        return 0;
    }

    Height ShowAssetInfo(IWalletDB::Ptr db, const storage::Totals::AssetTotals& totals)
    {
        auto isOwned  = false;
        std::string coinName = kNA;
        std::string unitName = kAmountASSET;
        std::string nthName  = kAmountAGROTH;
        std::string ownerStr = kNA;
        std::string lkHeight = kNA;
        std::string rfHeight = kNA;
        std::string emission = kNA;

        const auto info = db->findAsset(totals.AssetId);
        if (info.is_initialized())
        {
            const WalletAssetMeta &meta = info.is_initialized() ? WalletAssetMeta(*info) : WalletAssetMeta(Asset::Full());
            isOwned  = info->m_IsOwned;
            unitName = meta.GetUnitName();
            nthName  = meta.GetNthUnitName();
            ownerStr = (isOwned ? info->m_Owner.str() + "\nYou own this asset": info->m_Owner.str());
            coinName = meta.GetName() + " (" + meta.GetShortName() + ")";
            lkHeight = std::to_string(info->m_LockHeight);
            rfHeight = std::to_string(info->m_RefreshHeight);

            std::stringstream ss;
            ss << PrintableAmount(info->m_Value, true, unitName, nthName);
            emission = ss.str();
        }

        const unsigned kWidth = 26;
        cout << boost::format(kWalletAssetSummaryFormat)
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetIDFormat) % totals.AssetId
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetNameFormat) % coinName
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetLockHeightFormat) % lkHeight
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetRefreshHeightFormat) % rfHeight
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetEmissionFormat) % emission
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetOwnerFormat) % ownerStr
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailable) % to_string(PrintableAmount(totals.Avail, false, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldInProgress) % to_string(PrintableAmount(totals.Incoming, false, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldUnavailable) % to_string(PrintableAmount(totals.Unavail, false, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalUnspent) % to_string(PrintableAmount(totals.Unspent, false, unitName, nthName));
             // % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldShielded) % to_string(PrintableAmount(totals.Shielded, false, unitName, nthName));

        if (!info.is_initialized())
        {
            cout << kWalletNoInfo;
        }
        else if (totals.MinCoinHeight && info->m_LockHeight > totals.MinCoinHeight)
        {
            cout << boost::format(kWalletUnreliableAsset) % info->m_LockHeight;
        }

        return info ? info->m_LockHeight : 0;
    }

    std::pair<std::string, std::string> GetAssetNames(IWalletDB::Ptr walletDB, Asset::ID assetId)
    {
        std::string unitName = kBEAM;
        std::string nthName  = kGROTH;

        if (assetId != Asset::s_InvalidID)
        {
            const auto info = walletDB->findAsset(assetId);
            const WalletAssetMeta &meta = info.is_initialized() ? WalletAssetMeta(*info) : WalletAssetMeta(Asset::Full());
            unitName = meta.GetUnitName();
            nthName  = meta.GetNthUnitName();
        }

        return std::make_pair(unitName, nthName);
    }

    Height GetAssetLockHeight(IWalletDB::Ptr walletDB, Asset::ID assetId)
    {
        if (assetId != Asset::s_InvalidID)
        {
            const auto info = walletDB->findAsset(assetId);
            if (info.is_initialized())
            {
                return info->m_LockHeight;
            }
        }
        return 0;
    }

    void ShowAssetCoins(const IWalletDB::Ptr& walletDB, Asset::ID assetId)
    {
        const auto [unitName, nthName] = GetAssetNames(walletDB, assetId);
        const uint8_t idWidth = assetId == Asset::s_InvalidID ? 49 : 57;
        const array<uint8_t, 6> columnWidths{{idWidth, 14, 14, 18, 20, 8}};

        const auto lockHeight = GetAssetLockHeight(walletDB, assetId);
        std::vector<Coin> reliable;
        std::vector<Coin> unreliable;

        walletDB->visitCoins([&](const Coin& c)->bool {
            if (c.m_ID.m_AssetID == assetId)
            {
                if (c.m_confirmHeight < lockHeight)
                {
                    unreliable.push_back(c);
                }
                else
                {
                    reliable.push_back(c);
                }
            }
            return true;
        });

        auto offset = walletDB->getCoinConfirmationsOffset();
        const auto displayCoins = [&](const std::vector<Coin>& coins) {
            if (coins.empty())
            {
                return;
            }
            for(const auto& c: coins) {
                 cout << boost::format(kCoinsTableFormat)
                        % boost::io::group(left, setw(columnWidths[0]), c.toStringID())
                        % boost::io::group(right, setw(columnWidths[1]), c.m_ID.m_Value / Rules::Coin)
                        % boost::io::group(right, setw(columnWidths[2]), c.m_ID.m_Value % Rules::Coin)
                        % boost::io::group(left, setw(columnWidths[3]),  (c.IsMaturityValid() ? std::to_string(static_cast<int64_t>(c.get_Maturity(offset))) : "-"))
                        % boost::io::group(left, setw(columnWidths[4]), getCoinStatus(c.m_status))
                        % boost::io::group(left, setw(columnWidths[5]), c.m_ID.m_Type)
                      << std::endl;
            }
        };

        const bool hasCoins = !(reliable.empty() && unreliable.empty());
        if (hasCoins)
        {
            cout << boost::format(kCoinsTableHeadFormat)
                     % boost::io::group(left, setw(columnWidths[0]), kCoinColumnId)
                     % boost::io::group(right, setw(columnWidths[1]), unitName)
                     % boost::io::group(right, setw(columnWidths[2]), nthName)
                     % boost::io::group(left, setw(columnWidths[3]), kCoinColumnMaturity)
                     % boost::io::group(left, setw(columnWidths[4]), kCoinColumnStatus)
                     % boost::io::group(left, setw(columnWidths[5]), kCoinColumnType)
                  << std::endl;

            displayCoins(reliable);
            if (!unreliable.empty())
            {
                cout << kTxHistoryUnreliableCoins;
                displayCoins(unreliable);
            }
        }
        else
        {
            cout << kNoCoins;
        }

        cout << endl;
    }

    void ShowAssetTxs(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, Asset::ID assetId)
    {
        // Lelantus basic ops disabled starting from v5.1
        bool show = vm.count(cli::TX_HISTORY); // || vm.count(cli::SHIELDED_TX_HISTORY);
        if (!show) return;

        std::vector<TxDescription> txHistory;

        if (vm.count(cli::TX_HISTORY))
        {
            auto txSimple = walletDB->getTxHistory(TxType::Simple);
            auto txReg = walletDB->getTxHistory(TxType::AssetReg);
            auto txIssue = walletDB->getTxHistory(TxType::AssetIssue);
            auto txConsume = walletDB->getTxHistory(TxType::AssetConsume);
            auto txUnreg = walletDB->getTxHistory(TxType::AssetUnreg);
            auto txInfo = walletDB->getTxHistory(TxType::AssetInfo);

            if (assetId != Asset::s_InvalidID)
            {
                txHistory.insert(txHistory.end(), txSimple.begin(), txSimple.end());
            }

            txHistory.insert(txHistory.end(), txReg.begin(), txReg.end());
            txHistory.insert(txHistory.end(), txIssue.begin(), txIssue.end());
            txHistory.insert(txHistory.end(), txConsume.begin(), txConsume.end());
            txHistory.insert(txHistory.end(), txUnreg.begin(), txUnreg.end());
            txHistory.insert(txHistory.end(), txInfo.begin(), txInfo.end());
        }

        // Lelantus basic ops disabled starting from v5.1
        //if (vm.count(cli::SHIELDED_TX_HISTORY))
        //{
        //    // there cannot be 'orphaned' asset push/pull transactions but beam only, so skip in this case
        //    if (assetId != Asset::s_InvalidID)
        //    {
        //        auto pushTxHistory = walletDB->getTxHistory(TxType::PushTransaction);
        //        auto pullTxHistory = walletDB->getTxHistory(TxType::PullTransaction);
        //        txHistory.insert(txHistory.end(), pushTxHistory.begin(), pushTxHistory.end());
        //        txHistory.insert(txHistory.end(), pullTxHistory.begin(), pullTxHistory.end());
        //    }
        //}

        std::sort(txHistory.begin(), txHistory.end(), [](const TxDescription& a, const TxDescription& b) -> bool {
            return a.m_createTime > b.m_createTime;
        });

        txHistory.erase(std::remove_if(txHistory.begin(), txHistory.end(), [&assetId](const auto& tx) {
            return tx.m_assetId != assetId;
        }), txHistory.end());

        if (txHistory.empty())
        {
            cout << kTxHistoryEmpty << endl;
        }
        else
        {
            const auto lockHeight = GetAssetLockHeight(walletDB, assetId);
            auto [unitName, nthName] = GetAssetNames(walletDB, assetId);
            if (assetId == Asset::s_InvalidID)
            {
                unitName = kAmountASSET;
                nthName = kAmountAGROTH;
            }

            boost::ignore_unused(nthName);
            const auto amountHeader = boost::format(kAssetTxHistoryColumnAmount) % unitName;

            const array<uint8_t, 7> columnWidths{{20, 10, 17, 18, 20, 33, 65}};
                cout << boost::format(kTxHistoryTableHead)
                        % boost::io::group(left,  setw(columnWidths[0]),  kTxHistoryColumnDatetTime)
                        % boost::io::group(left,  setw(columnWidths[1]),  kTxHistoryColumnHeight)
                        % boost::io::group(left,  setw(columnWidths[2]),  kTxHistoryColumnDirection)
                        % boost::io::group(right, setw(columnWidths[3]),  amountHeader)
                        % boost::io::group(left,  setw(columnWidths[4]),  kTxHistoryColumnStatus)
                        % boost::io::group(left,  setw(columnWidths[5]),  kTxHistoryColumnId)
                        % boost::io::group(left,  setw(columnWidths[6]),  kTxHistoryColumnKernelId)
                     << std::endl;

            bool unreliableDisplayed = false;
            for (auto& tx : txHistory) {
                std::string direction = tx.m_sender ? kTxDirectionOut : kTxDirectionIn;
                if (tx.m_txType == TxType::AssetInfo || tx.m_txType == TxType::AssetReg || tx.m_txType == TxType::AssetUnreg ||
                    tx.m_txType == TxType::AssetIssue || tx.m_txType == TxType::AssetConsume)
                {
                    direction = tx.getTxTypeString();
                }
                else if(tx.m_selfTx)
                {
                    direction = kTxDirectionSelf;
                }

                std::string amount = to_string(PrintableAmount(tx.m_amount, true));
                if (tx.m_txType == TxType::AssetReg)
                {
                    amount += kBeamFee;
                }
                else if (tx.m_txType == TxType::AssetUnreg)
                {
                    amount += kBeamRefund;
                }
                else if (tx.m_txType == TxType::AssetInfo)
                {
                    amount = kNA;
                }

                std::string kernelId = to_string(tx.m_kernelID);
                if (tx.m_txType == TxType::AssetInfo)
                {
                    kernelId = kNA;
                }

                Height height = storage::DeduceTxDisplayHeight(*walletDB, tx);
                if (height < lockHeight && !unreliableDisplayed)
                {
                    unreliableDisplayed = true;
                    cout << kTxHistoryUnreliableTxs;
                }

                auto statusInterpreter = walletDB->getStatusInterpreter(tx);
                cout << boost::format(kTxHistoryTableFormat)
                        % boost::io::group(left,  setw(columnWidths[0]),  format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                        % boost::io::group(left,  setw(columnWidths[1]),  static_cast<int64_t>(height))
                        % boost::io::group(left,  setw(columnWidths[2]),  direction)
                        % boost::io::group(right, setw(columnWidths[3]),  amount)
                        % boost::io::group(left,  setw(columnWidths[4]),  statusInterpreter.getStatus())
                        % boost::io::group(left,  setw(columnWidths[5]),  to_hex(tx.m_txId.data(), tx.m_txId.size()))
                        % boost::io::group(left,  setw(columnWidths[6]),  kernelId)
                     << std::endl;
            }
        }
    }

    void ShowShilededCoins(const po::variables_map& vm, IWalletDB::Ptr walletDB, Asset::ID assetId)
    {
        if (!vm.count(cli::SHIELDED_UTXOS))
        {
            return;
        }

        // TODO should implement
        const char kShieldedCoinsTableHeadFormat[] = "  | %1% | %2% | %3% | %4% | %5% | %6% | %7% | %8% | %9% |";
        const char kShieldedCreateTxID[]    = "createTxID";
        const char kShieldedSpentTxID[]     = "spentTxID";
        const char kShieldedConfirmHeight[] = "confirmHeight";
        const char kShieldedSpentHeight[]   = "spentHeight";
        const char kUnlinkProgress[]          = "unlink(%)";
        const char kTargetAnonymitySet[]    = "targetAnonymitySet";

        auto shieldedCoins = walletDB->getShieldedCoins(assetId);
        if (shieldedCoins.empty())
        {
            std::cout << kNoShieldedCoins << std::endl;
            return;
        }

        const auto lockHeight = GetAssetLockHeight(walletDB, assetId);
        auto [unitName, nthName] = GetAssetNames(walletDB, assetId);

        std::vector<ShieldedCoin> reliable;
        std::vector<ShieldedCoin> unreliable;

        for (const auto& c: shieldedCoins) {
            if (c.m_confirmHeight < lockHeight)
            {
                unreliable.push_back(c);
            }
            else
            {
                reliable.push_back(c);
            }
        }

        const uint8_t nameWidth = std::max<uint8_t>(10, static_cast<uint8_t>(unitName.size()));
        const uint8_t nthWidth  = std::max<uint8_t>(10, static_cast<uint8_t>(nthName.size()));

        const array<uint8_t, 9> columnWidths{ { 10, nameWidth, nthWidth, 32, 32, 13, 11, 20, 18} };
        cout << "SHIELDED COINS\n\n"
             << boost::format(kShieldedCoinsTableHeadFormat)
                % boost::io::group(left, setw(columnWidths[0]),  kCoinColumnId)
                % boost::io::group(right, setw(columnWidths[1]), unitName)
                % boost::io::group(right, setw(columnWidths[2]), nthName)
                % boost::io::group(right, setw(columnWidths[3]), kShieldedCreateTxID)
                % boost::io::group(right, setw(columnWidths[4]), kShieldedSpentTxID)
                % boost::io::group(right, setw(columnWidths[5]), kShieldedConfirmHeight)
                % boost::io::group(right, setw(columnWidths[6]), kShieldedSpentHeight)
                % boost::io::group(right, setw(columnWidths[7]), kUnlinkProgress)
                % boost::io::group(right, setw(columnWidths[8]), kTargetAnonymitySet)
             << std::endl;

        const auto displayCoins = [&](const std::vector<ShieldedCoin>& coins) {
            for (const auto& c : coins)
            {
                cout << boost::format(kShieldedCoinsTableHeadFormat)
                    % boost::io::group(left, setw(columnWidths[0]),  c.m_TxoID == ShieldedCoin::kTxoInvalidID ? "--" : std::to_string(c.m_TxoID))
                    % boost::io::group(right, setw(columnWidths[1]), c.m_CoinID.m_Value / Rules::Coin)
                    % boost::io::group(right, setw(columnWidths[2]), c.m_CoinID.m_Value % Rules::Coin)
                    % boost::io::group(left, setw(columnWidths[3]),  c.m_createTxId ? to_hex(c.m_createTxId->data(), c.m_createTxId->size()) : "")
                    % boost::io::group(left, setw(columnWidths[4]),  c.m_spentTxId ? to_hex(c.m_spentTxId->data(), c.m_spentTxId->size()) : "")
                    % boost::io::group(right, setw(columnWidths[5]), (c.m_confirmHeight != MaxHeight) ? std::to_string(c.m_confirmHeight) : "--")
                    % boost::io::group(right, setw(columnWidths[6]), (c.m_spentHeight != MaxHeight) ? std::to_string(c.m_spentHeight) : "--")
                    % boost::io::group(right, setw(columnWidths[7]), std::to_string(c.m_UnlinkProgress))
                    % boost::io::group(right, setw(columnWidths[8]), Rules::get().Shielded.m_ProofMax.get_N())
                    << std::endl;
            }
        };

        displayCoins(reliable);
        if (!unreliable.empty())
        {
            cout << kTxHistoryUnreliableCoins;
            displayCoins(unreliable);
        }

        cout << std::endl;
    }

    void ShowAssetsInfo(const po::variables_map& vm)
    {
        auto showAssetId = Asset::s_InvalidID;
        if (vm.count(cli::ASSET_ID))
        {
            showAssetId = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
        }

        auto walletDB = OpenDataBase(vm);
        Block::SystemState::ID stateID = {};
        walletDB->getSystemStateID(stateID);
        storage::Totals totals(*walletDB);

        const auto displayAsset = [&vm, &walletDB](const storage::Totals::AssetTotals &totals) {
            cout << endl;
            ShowAssetInfo(walletDB, totals);
            ShowAssetCoins(walletDB, totals.AssetId);
            ShowShilededCoins(vm, walletDB, totals.AssetId);
            ShowAssetTxs(vm, walletDB, totals.AssetId);
        };

        if (showAssetId != Asset::s_InvalidID)
        {
            displayAsset(totals.GetTotals(showAssetId));
        }
        else
        {
            bool assetDisplayed = false;
            for (auto it : totals.allTotals)
            {
                const auto assetId = it.second.AssetId;
                if (assetId != Asset::s_InvalidID)
                {
                    displayAsset(it.second);
                    assetDisplayed = true;
                }
            }

            //
            // Totals counts only coins. There might be transactions for assets
            // that do not have coins. Also there might be asset transactions
            // that have 0 asset id.
            //
            if (vm.count(cli::TX_HISTORY))
            {
                std::set<Asset::ID> noCoins;

                const auto filter = [&](const std::vector<TxDescription> &vec) -> bool {
                    bool orphans = false;
                    for (const auto &tx: vec)
                    {
                        orphans = orphans || tx.m_assetId == Asset::s_InvalidID;
                        if (!totals.HasTotals(tx.m_assetId))
                        {
                            noCoins.insert(tx.m_assetId);
                        }
                    }
                    return orphans;
                };

                filter(walletDB->getTxHistory(TxType::Simple));
                bool hasOrphaned = filter(walletDB->getTxHistory(TxType::AssetReg));
                hasOrphaned = filter(walletDB->getTxHistory(TxType::AssetIssue)) || hasOrphaned;
                hasOrphaned = filter(walletDB->getTxHistory(TxType::AssetConsume)) || hasOrphaned;
                hasOrphaned = filter(walletDB->getTxHistory(TxType::AssetUnreg)) || hasOrphaned;
                hasOrphaned = filter(walletDB->getTxHistory(TxType::AssetInfo)) || hasOrphaned;

                for (auto assetId: noCoins)
                {
                    cout << endl << endl;
                    displayAsset(totals.GetTotals(assetId));
                    assetDisplayed = true;
                }

                if (hasOrphaned)
                {
                    cout << endl << endl << kOrphanedAseetTxs << endl;
                    ShowAssetTxs(vm, walletDB, 0);
                }

                if (!assetDisplayed && !hasOrphaned)
                {
                    // if any asset without transaction has been displayed before
                    // 'no transactions' message has been displayed as well.
                    cout << kNoAssetTxsInWallet << endl;
                }
            }

            if (!assetDisplayed)
            {
                cout << kNoAssetsInWallet << endl;
            }
        }
    }

    void ShowBEAMInfo(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        Block::SystemState::ID stateID = {};
        walletDB->getSystemStateID(stateID);
        storage::Totals totalsCalc(*walletDB);

        // Show info about BEAM
        const auto& totals = totalsCalc.GetBeamTotals();
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

        ShowAssetCoins(walletDB, Zero);
        ShowShilededCoins(vm, walletDB, Zero);

        // Lelantus basic ops disabled starting from v5.1
        if (vm.count(cli::TX_HISTORY) /*|| vm.count(cli::SHIELDED_TX_HISTORY)*/)
        {
            std::vector<TxDescription> txHistory;

            if (vm.count(cli::TX_HISTORY))
            {
                auto simpleTxHistory = walletDB->getTxHistory();
                txHistory.insert(txHistory.end(), simpleTxHistory.begin(), simpleTxHistory.end());
            }

            // Lelantus basic ops disabled starting from v5.1
            // if (vm.count(cli::SHIELDED_TX_HISTORY))
            // {
            //    auto pushTxHistory = walletDB->getTxHistory(TxType::PushTransaction);
            //    auto pullTxHistory = walletDB->getTxHistory(TxType::PullTransaction);
            //    txHistory.insert(txHistory.end(), pushTxHistory.begin(), pushTxHistory.end());
            //    txHistory.insert(txHistory.end(), pullTxHistory.begin(), pullTxHistory.end());
            //}

            txHistory.erase(std::remove_if(txHistory.begin(), txHistory.end(), [](const auto& tx) {
                return tx.m_assetId != 0;
            }), txHistory.end());

            if (txHistory.empty())
            {
                cout << kTxHistoryEmpty << endl;
            }
            else
            {
                const array<uint8_t, 7> columnWidths{ {20, 17, 26, 21, 33, 65, 100} };
                cout << boost::format(kTxHistoryTableHead)
                    % boost::io::group(left, setw(columnWidths[0]), kTxHistoryColumnDatetTime)
                    % boost::io::group(left, setw(columnWidths[1]), kTxHistoryColumnDirection)
                    % boost::io::group(right, setw(columnWidths[2]), kTxHistoryColumnAmount)
                    % boost::io::group(left, setw(columnWidths[3]), kTxHistoryColumnStatus)
                    % boost::io::group(left, setw(columnWidths[4]), kTxHistoryColumnId)
                    % boost::io::group(left, setw(columnWidths[5]), kTxHistoryColumnKernelId)
                    % boost::io::group(left, setw(columnWidths[6]), kTxToken)
                    << std::endl;

                for (auto& tx : txHistory) {
                    auto statusInterpreter = walletDB->getStatusInterpreter(tx);
                    cout << boost::format(kTxHistoryTableFormat)
                        % boost::io::group(left, setw(columnWidths[0]),
                            format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                        % boost::io::group(left, setw(columnWidths[1]),
                        (tx.m_selfTx ? kTxDirectionSelf : (tx.m_sender ? kTxDirectionOut
                            : kTxDirectionIn)))
                        % boost::io::group(right, setw(columnWidths[2]),
                            to_string(PrintableAmount(tx.m_amount, true)))
                        % boost::io::group(left, setw(columnWidths[3]), statusInterpreter.getStatus())
                        % boost::io::group(left, setw(columnWidths[4]), to_hex(tx.m_txId.data(), tx.m_txId.size()))
                        % boost::io::group(left, setw(columnWidths[5]), to_string(tx.m_kernelID))
                            % boost::io::group(left, setw(columnWidths[6]), tx.getToken())
                        << std::endl;
                }
            }
        }

        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
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
                cout << boost::format(kSwapTxHistoryTableHead)
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

                    auto statusInterpreter = walletDB->getStatusInterpreter(tx);
                    cout << boost::format(kSwapTxHistoryTableFormat)
                        % boost::io::group(left, setw(columnWidths[0]),
                            format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                        % boost::io::group(right, setw(columnWidths[1]),
                            to_string(PrintableAmount(tx.m_amount, true)))
                        % boost::io::group(right, setw(columnWidths[2]), swapAmount)
                        % boost::io::group(right, setw(columnWidths[3]), ss.str())
                        % boost::io::group(left, setw(columnWidths[4]), statusInterpreter.getStatus())
                        % boost::io::group(left, setw(columnWidths[5]), to_hex(tx.m_txId.data(), tx.m_txId.size()))
                        << std::endl;
                }
            }
        }
        #endif
    }

    int ShowWalletInfo(const po::variables_map& vm)
    {
        //
        // If asset info is requested we show only it
        // Otherwise we display both BEAM & ASSETS
        //
        const bool showAssets = vm.count(cli::ASSETS) != 0 || vm.count(cli::ASSET_ID) != 0;
        if (showAssets)
        {
            ShowAssetsInfo(vm);
        }
        else
        {
            ShowBEAMInfo(vm);
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
        auto token = tx->getToken();
        auto statusInterpreter = walletDB->getStatusInterpreter(*tx);
        LOG_INFO()
            << boost::format(kTxDetailsFormat)
                % storage::TxDetailsInfo(walletDB, *txId) % statusInterpreter.getStatus()
            << (tx->m_status == TxStatus::Failed
                    ? boost::format(kTxDetailsFailReason) % GetFailureMessage(tx->m_failureReason)
                    : boost::format(""))
            << (!token.empty() ? "\nToken: " : "") << token;


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
        const auto timestampedPath = TimestampFile(path);

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
        auto addressOrToken = vm[cli::RECEIVER_ADDR].as<string>();
        auto receiverParams = ParseParameters(addressOrToken);
        if (!receiverParams)
        {
            LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }
        if (!LoadReceiverParams(*receiverParams, params))
        {
            return false;
        }
        if (auto peerID = params.GetParameter<WalletID>(beam::wallet::TxParameterID::PeerID); peerID && std::to_string(*peerID) != addressOrToken)
        {
            params.SetParameter(beam::wallet::TxParameterID::OriginalToken, addressOrToken);
        }
        return true;
    }

    bool ReadAmount(const po::variables_map& vm, Amount& amount, const Amount& limit = std::numeric_limits<Amount>::max(), bool asset = false)
    {
        if (vm.count(cli::AMOUNT) == 0)
        {
            LOG_ERROR() << kErrorAmountMissing;
            return false;
        }

        const auto strAmount = vm[cli::AMOUNT].as<std::string>();

        try
        {
            boost::multiprecision::cpp_dec_float_50 preciseAmount(strAmount.c_str());
            preciseAmount *= Rules::Coin;

            if (preciseAmount == 0)
            {
                LOG_ERROR() << kErrorZeroAmount;
                return false;
            }

            if (preciseAmount < 0)
            {
                LOG_ERROR() << (boost::format(kErrorNegativeAmount) % strAmount).str();
                return false;
            }

            if (preciseAmount > limit)
            {
                std::stringstream ssLimit;
                ssLimit << PrintableAmount(limit, false, asset ? kAmountASSET : "", asset ? kAmountAGROTH : "");
                LOG_ERROR() << (boost::format(kErrorTooBigAmount) % strAmount % ssLimit.str()).str();
                return false;
            }

             amount = preciseAmount.convert_to<Amount>();
        }
        catch(const std::runtime_error& err)
        {
            LOG_ERROR() << "the argument ('" << strAmount << "') for option '--amount' is invalid.";
            LOG_ERROR() << err.what();
            return false;
        }

        return true;
    }

    bool ReadFee(const po::variables_map& vm, Amount& fee, bool checkFee)
    {
        fee = vm[cli::FEE].as<Nonnegative<Amount>>().value;
        if (checkFee && fee < cli::kMinimumFee)
        {
            LOG_ERROR() << kErrorFeeToLow;
            return false;
        }

        return true;
    }

    bool ReadShieldedId(const po::variables_map& vm, TxoID& id)
    {
        if (vm.count(cli::SHIELDED_ID) == 0)
        {
            LOG_ERROR() << kErrorShieldedIDMissing;
            return false;
        }

        id = vm[cli::SHIELDED_ID].as<Nonnegative<TxoID>>().value;

        return true;
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

        if (!ReadAmount(vm, amount))
        {
            return false;
        }

        if (!ReadFee(vm, fee, checkFee))
        {
            return false;
        }

        if(vm.count(cli::ASSET_ID)) // asset id can be zero if beam only
        {
            assetId = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
        }

        return true;
    }

    #ifdef BEAM_ATOMIC_SWAP_SUPPORT
    template<typename Settings>
    bool ParseElectrumSettings(const po::variables_map& vm, Settings& settings)
    {
        if (vm.count(cli::ELECTRUM_SEED) || vm.count(cli::ELECTRUM_ADDR) ||
            vm.count(cli::GENERATE_ELECTRUM_SEED) || vm.count(cli::SELECT_SERVER_AUTOMATICALLY) ||
            vm.count(cli::ADDRESSES_TO_RECEIVE) || vm.count(cli::ADDRESSES_FOR_CHANGE))
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

            if (vm.count(cli::ADDRESSES_TO_RECEIVE))
            {
                electrumSettings.m_receivingAddressAmount = vm[cli::ADDRESSES_TO_RECEIVE].as<Positive<uint32_t>>().value;
            }

            if (vm.count(cli::ADDRESSES_FOR_CHANGE))
            {
                electrumSettings.m_changeAddressAmount = vm[cli::ADDRESSES_FOR_CHANGE].as<Positive<uint32_t>>().value;
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

                // TODO roman.strilets need to check words
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
    int HandleSwapCoin(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, const char* swapCoin)
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
            Amount feeRate = vm[cli::SWAP_FEERATE].as<Positive<Amount>>().value;
            if (feeRate < settings.GetMinFeeRate())
            {
                ostringstream stream;
                stream << "Error: you set fee rate less than minimun. For " << swapCoin << " it should be > " << settings.GetMinFeeRate();
                throw std::runtime_error(stream.str());
            }
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

                stream << "Amount of receiving addresses: " << settings.GetElectrumConnectionOptions().m_receivingAddressAmount << '\n';
                stream << "Amount of change addresses: " << settings.GetElectrumConnectionOptions().m_changeAddressAmount << '\n';
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
                return HandleSwapCoin<bitcoin::SettingsProvider, bitcoin::Settings, bitcoin::BitcoinCoreSettings, bitcoin::ElectrumSettings>
                    (vm, walletDB, kSwapCoinBTC);
            }
            case beam::wallet::AtomicSwapCoin::Litecoin:
            {
                return HandleSwapCoin<litecoin::SettingsProvider, litecoin::Settings, litecoin::LitecoinCoreSettings, litecoin::ElectrumSettings>
                    (vm, walletDB, kSwapCoinLTC);
            }
            case beam::wallet::AtomicSwapCoin::Qtum:
            {
                return HandleSwapCoin<qtum::SettingsProvider, qtum::Settings, qtum::QtumCoreSettings, qtum::ElectrumSettings>
                    (vm, walletDB, kSwapCoinQTUM);
            }
            case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
            {
                return HandleSwapCoin<bitcoin_cash::SettingsProvider, bitcoin_cash::Settings, bitcoin_cash::CoreSettings, bitcoin_cash::ElectrumSettings>
                    (vm, walletDB, kSwapCoinBCH);
            }
            case beam::wallet::AtomicSwapCoin::Bitcoin_SV:
            {
                return HandleSwapCoin<bitcoin_sv::SettingsProvider, bitcoin_sv::Settings, bitcoin_sv::CoreSettings, bitcoin_sv::ElectrumSettings>
                    (vm, walletDB, kSwapCoinBSV);
            }
            case beam::wallet::AtomicSwapCoin::Dogecoin:
            {
                return HandleSwapCoin<dogecoin::SettingsProvider, dogecoin::Settings, dogecoin::DogecoinCoreSettings, dogecoin::ElectrumSettings>
                    (vm, walletDB, kSwapCoinDOGE);
            }
            case beam::wallet::AtomicSwapCoin::Dash:
            {
                return HandleSwapCoin<dash::SettingsProvider, dash::Settings, dash::DashCoreSettings, dash::ElectrumSettings>
                    (vm, walletDB, kSwapCoinDASH);
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
            case beam::wallet::AtomicSwapCoin::Qtum:
            {
                ShowSwapSettings<qtum::SettingsProvider>(walletDB, "qtum");
                break;
            }
            case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
            {
                ShowSwapSettings<bitcoin_cash::SettingsProvider>(walletDB, "bch");
                break;
            }
            case beam::wallet::AtomicSwapCoin::Bitcoin_SV:
            {
                ShowSwapSettings<bitcoin_sv::SettingsProvider>(walletDB, "bsv");
                break;
            }
            case beam::wallet::AtomicSwapCoin::Dogecoin:
            {
                ShowSwapSettings<dogecoin::SettingsProvider>(walletDB, "dogecoin");
                break;
            }
            case beam::wallet::AtomicSwapCoin::Dash:
            {
                ShowSwapSettings<dash::SettingsProvider>(walletDB, "dash");
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
        else if (swapCoin == wallet::AtomicSwapCoin::Bitcoin_Cash)
        {
            auto bchSettingsProvider = std::make_shared<bitcoin_cash::SettingsProvider>(walletDB);
            bchSettingsProvider->Initialize();
            auto bchSettings = bchSettingsProvider->GetSettings();
            if (!bchSettings.IsInitialized())
            {
                throw std::runtime_error("Bitcoin Cash settings should be initialized.");
            }

            if (!BitcoinCashSide::CheckAmount(*swapAmount, bchSettings.GetFeeRate()))
            {
                throw std::runtime_error("The swap amount must be greater than the redemption fee.");
            }
            swapFeeRate = bchSettings.GetFeeRate();
        }
        else if (swapCoin == wallet::AtomicSwapCoin::Bitcoin_SV)
        {
            auto bsvSettingsProvider = std::make_shared<bitcoin_sv::SettingsProvider>(walletDB);
            bsvSettingsProvider->Initialize();
            auto bsvSettings = bsvSettingsProvider->GetSettings();
            if (!bsvSettings.IsInitialized())
            {
                throw std::runtime_error("Bitcoin SV settings should be initialized.");
            }

            if (!BitcoinSVSide::CheckAmount(*swapAmount, bsvSettings.GetFeeRate()))
            {
                throw std::runtime_error("The swap amount must be greater than the redemption fee.");
            }
            swapFeeRate = bsvSettings.GetFeeRate();
        }
        else if (swapCoin == wallet::AtomicSwapCoin::Dogecoin)
        {
            auto dogecoinSettingsProvider = std::make_shared<dogecoin::SettingsProvider>(walletDB);
            dogecoinSettingsProvider->Initialize();
            auto dogecoinSettings = dogecoinSettingsProvider->GetSettings();
            if (!dogecoinSettings.IsInitialized())
            {
                throw std::runtime_error("Dogecoin settings should be initialized.");
            }

            if (!DogecoinSide::CheckAmount(*swapAmount, dogecoinSettings.GetFeeRate()))
            {
                throw std::runtime_error("The swap amount must be greater than the redemption fee.");
            }
            swapFeeRate = dogecoinSettings.GetFeeRate();
        }
        else if (swapCoin == wallet::AtomicSwapCoin::Dash)
        {
            auto dashSettingsProvider = std::make_shared<dash::SettingsProvider>(walletDB);
            dashSettingsProvider->Initialize();
            auto dashSettings = dashSettingsProvider->GetSettings();
            if (!dashSettings.IsInitialized())
            {
                throw std::runtime_error("Dash settings should be initialized.");
            }

            if (!DashSide::CheckAmount(*swapAmount, dashSettings.GetFeeRate()))
            {
                throw std::runtime_error("The swap amount must be greater than the redemption fee.");
            }
            swapFeeRate = dashSettings.GetFeeRate();
        }
        else
        {
            throw std::runtime_error("Unsupported swap coin.");
        }

#ifdef BEAM_LIB_VERSION
        if (auto libVersion = swapTxParameters->GetParameter(beam::wallet::TxParameterID::LibraryVersion); libVersion)
        {
            std::string libVersionStr;
            beam::wallet::fromByteBuffer(*libVersion, libVersionStr);
            std::string myLibVersionStr = BEAM_LIB_VERSION;

            std::regex libVersionRegex("\\d{1,}\\.\\d{1,}\\.\\d{4,}");
            if (std::regex_match(libVersionStr, libVersionRegex) &&
                std::lexicographical_compare(
                    myLibVersionStr.begin(),
                    myLibVersionStr.end(),
                    libVersionStr.begin(),
                    libVersionStr.end(),
                    std::less<char>{}))
            {
                LOG_WARNING() <<
                    "This token generated by newer Beam library version(" << libVersionStr << ")\n" <<
                    "Your version is: " << myLibVersionStr << " Please, check for updates.";
            }
        }
#endif  // BEAM_LIB_VERSION

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
    #endif // BEAM_ATOMIC_SWAP_SUPPORT

    struct CliNodeConnection final : public proto::FlyClient::NetworkStd
    {
    public:
        CliNodeConnection(proto::FlyClient& fc) : proto::FlyClient::NetworkStd(fc) {};
        void OnConnectionFailed(const proto::NodeConnection::DisconnectReason& reason) override
        {
            LOG_ERROR() << kErrorConnectionFailed << " reason: " << reason;
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

    void CheckAssetsAllowed(const po::variables_map& vm)
    {
        if (!Rules::get().CA.Enabled)
        {
            throw std::runtime_error(kErrorAssetsFork2);
        }

        if (!vm[cli::WITH_ASSETS].as<bool>())
        {
            throw std::runtime_error(kErrorAssetsDisabled);
        }
    }

    std::string ReadAssetMeta(const po::variables_map& vm, bool allow_v5_0)
    {
        if(!vm.count(cli::ASSET_METADATA))
        {
            throw std::runtime_error(kErrorAssetMetadataRequired);
        }

        std::string strMeta = vm[cli::ASSET_METADATA].as<std::string>();
        if (strMeta.empty())
        {
            throw std::runtime_error(kErrorAssetMetadataRequired);
        }

        WalletAssetMeta meta(strMeta);

        if (!(allow_v5_0 ? meta.isStd_v5_0() : meta.isStd()))
        {
            throw std::runtime_error(kErrorAssetNonSTDMeta);
        }

        return strMeta;
    }

    std::string AssetID2Meta(const po::variables_map& vm, IWalletDB::Ptr walletDB)
    {
        if(!vm.count(cli::ASSET_ID))
        {
            throw std::runtime_error(kErrorAssetIDRequired);
        }

        const Asset::ID assetID = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
        const auto info = walletDB->findAsset(assetID);
        if(!info.is_initialized())
        {
             throw std::runtime_error(kErrorAssetNotFound);
        }

        if(!info->m_IsOwned)
        {
            throw std::runtime_error(kErrorAssetNotOwned);
        }

        std::string meta;
        if(!fromByteBuffer(info->m_Metadata.m_Value, meta))
        {
            throw std::runtime_error(kErrorAssetLoadMeta);
        }

        return meta;
    }

    boost::optional<TxID> IssueConsumeAsset(bool issue, const po::variables_map& vm, Wallet& wallet, IWalletDB::Ptr walletDB)
    {
        CheckAssetsAllowed(vm);

        std::string meta;
        if(vm.count(cli::ASSET_ID))
        {
            meta = AssetID2Meta(vm, walletDB);
        }
        else if (vm.count(cli::ASSET_METADATA))
        {
            meta = ReadAssetMeta(vm, true);
        }
        else
        {
            throw std::runtime_error(kErrorAssetIdOrMetaRequired);
        }

        Amount amountGroth = 0;
        Amount limit = static_cast<Amount>(std::numeric_limits<AmountSigned>::max());
        if(!ReadAmount(vm, amountGroth, limit, true))
        {
            return boost::none;
        }

        Amount fee = 0;
        if(!ReadFee(vm, fee, true))
        {
            return boost::none;
        }

        auto params = CreateTransactionParameters(issue ? TxType::AssetIssue : TxType::AssetConsume)
                        .SetParameter(TxParameterID::Amount, amountGroth)
                        .SetParameter(TxParameterID::Fee, fee)
                        .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm))
                        .SetParameter(TxParameterID::AssetMetadata, meta);

        return wallet.StartTransaction(params);
    }

    boost::optional<TxID> RegisterAsset(const po::variables_map& vm, Wallet& wallet)
    {
        CheckAssetsAllowed(vm);

        const auto strMeta = ReadAssetMeta(vm, false);

        Amount fee = 0;
        if (!ReadFee(vm, fee, true))
        {
            return boost::none;
        }

        auto params = CreateTransactionParameters(TxType::AssetReg)
                        .SetParameter(TxParameterID::Amount, Rules::get().CA.DepositForList)
                        .SetParameter(TxParameterID::Fee, fee)
                        .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm))
                        .SetParameter(TxParameterID::AssetMetadata, strMeta);

        return wallet.StartTransaction(params);
    }

    boost::optional<TxID> UnregisterAsset(const po::variables_map& vm, Wallet& wallet, IWalletDB::Ptr walletDB)
    {
        CheckAssetsAllowed(vm);

        std::string meta;
        if(vm.count(cli::ASSET_ID))
        {
            meta = AssetID2Meta(vm, walletDB);
        }
        else if (vm.count(cli::ASSET_METADATA))
        {
            meta = ReadAssetMeta(vm, true);
        }
        else
        {
            throw std::runtime_error(kErrorAssetIdOrMetaRequired);
        }

        Amount fee = 0;
        if (!ReadFee(vm, fee, true))
        {
            return boost::none;
        }

        auto params = CreateTransactionParameters(TxType::AssetUnreg)
                        .SetParameter(TxParameterID::Amount, Rules::get().CA.DepositForList)
                        .SetParameter(TxParameterID::Fee, fee)
                        .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm))
                        .SetParameter(TxParameterID::AssetMetadata, meta);

        return wallet.StartTransaction(params);
    }

    TxID GetAssetInfo(const po::variables_map& vm, Wallet& wallet)
    {
        CheckAssetsAllowed(vm);

        auto params = CreateTransactionParameters(TxType::AssetInfo);
        if (vm.count(cli::ASSET_ID))
        {
            Asset::ID aid = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
            params.SetParameter(TxParameterID::AssetID, aid);
            return wallet.StartTransaction(params);
        }

        if (vm.count(cli::ASSET_METADATA))
        {
            const auto assetMeta = ReadAssetMeta(vm, true);
            params.SetParameter(TxParameterID::AssetMetadata, assetMeta);
            return wallet.StartTransaction(params);
        }

        throw std::runtime_error(kErrorAssetIdOrMetaRequired);
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
        laser->ListenClosedChannelsWithPossibleRollback();

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

    int DoWalletFunc(const po::variables_map& vm, std::function<int (const po::variables_map&, Wallet::Ptr, IWalletDB::Ptr, boost::optional<TxID>&, bool)> func)
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

        const auto withAssets = vm[cli::WITH_ASSETS].as<bool>();
        auto txCompletedAction = isServer ? Wallet::TxCompletedAction() : onTxCompleteAction;

        auto wallet = std::make_shared<Wallet>(walletDB, withAssets,
                      std::move(txCompletedAction),
                      Wallet::UpdateCompletedAction());
        {
            wallet::AsyncContextHolder holder(*wallet);

#ifdef BEAM_LELANTUS_SUPPORT
            // Forcibly disable starting from v5.1
            // lelantus::RegisterCreators(*wallet, walletDB, withAssets);
#endif

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
            RegisterSwapTxCreators(wallet, walletDB);
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
#ifdef BEAM_CONFIDENTIAL_ASSETS_SUPPORT
           if (Rules::get().CA.Enabled && withAssets)
            {
                RegisterAssetCreators(*wallet);
            }
#endif  // BEAM_CONFIDENTIAL_ASSETS_SUPPORT
            wallet->ResumeAllTransactions();

            auto nnet = CreateNetwork(*wallet, vm);
            if (!nnet)
            {
                return -1;
            }
            wallet->AddMessageEndpoint(make_shared<WalletNetworkViaBbs>(*wallet, nnet, walletDB));
            wallet->SetNodeEndpoint(nnet);

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

                if (assetId != Asset::s_InvalidID)
                {
                    CheckAssetsAllowed(vm);
                }

                WalletAddress senderAddress = GenerateNewAddress(walletDB, "");
                auto params = CreateSimpleTransactionParameters();
                LoadReceiverParams(vm, params);
                params.SetParameter(TxParameterID::MyID, senderAddress.m_walletID)
                    .SetParameter(TxParameterID::Amount, amount)
                    .SetParameter(TxParameterID::Fee, fee)
                    .SetParameter(TxParameterID::AssetID, assetId)
                    .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm));
                currentTxID = wallet->StartTransaction(params);

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
                wallet->Rescan();
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
                    if (wallet->CanCancelTransaction(*txId))
                    {
                        currentTxID = *txId;
                        wallet->CancelTransaction(*txId);
                        return 0;
                    }
                    auto statusInterpreter = walletDB->getStatusInterpreter(*tx);
                    LOG_ERROR() << kErrorCancelTxInInvalidStatus << statusInterpreter.getStatus();
                    return -1;
                }
                LOG_ERROR() << kErrorTxIdUnknown;
                return -1;
            });
    }

    #ifdef BEAM_ATOMIC_SWAP_SUPPORT
    int InitSwap(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                if (!wallet->IsWalletInSync())
                {
                    return -1;
                }

                currentTxID = InitSwap(vm, walletDB, *wallet, isFork1);
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
                currentTxID = AcceptSwap(vm, walletDB, *wallet, isFork1);
                if (!currentTxID)
                {
                    return -1;
                }
                return 0;
            });
    }
    #endif

    int IssueAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = IssueConsumeAsset(true, vm, *wallet, walletDB);
                return currentTxID ? 0 : -1;
            });
    }

    int ConsumeAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = IssueConsumeAsset(false, vm, *wallet, walletDB);
                return currentTxID ? 0 : -1;
            });
    }

    int RegisterAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = RegisterAsset(vm, *wallet);
                return currentTxID ? 0 : -1;
            });
    }

    int UnregisterAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = UnregisterAsset(vm, *wallet, walletDB);
                return currentTxID ? 0 : -1;
            });
    }

    int GetAssetInfo(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = GetAssetInfo(vm, *wallet);
                return currentTxID ? 0: -1;
            });
    }

    boost::optional<TxID> InsertToShieldedPool(const po::variables_map& vm, Wallet& wallet, IWalletDB::Ptr walletDB)
    {
        Amount amount = 0;
        Amount fee = 0;
        if (!ReadAmount(vm, amount) || !ReadFee(vm, fee, true))
        {
            return boost::none;
        }

        if (fee < kShieldedTxMinFeeInGroth)
        {
            LOG_ERROR() << "Fee is too small. Minimal fee is " << PrintableAmount(kShieldedTxMinFeeInGroth);
            return boost::none;
        }

        const Amount pushTxMinAmount = kShieldedTxMinFeeInGroth + 1;
        if (amount < pushTxMinAmount)
        {
            LOG_ERROR() << "Amount is too small. Minimal amount is " << PrintableAmount(pushTxMinAmount);
            return boost::none;
        }

        Asset::ID assetId = Asset::s_InvalidID;
        if(vm.count(cli::ASSET_ID)) // asset id can be zero if beam only
        {
            CheckAssetsAllowed(vm);
            assetId = vm[cli::ASSET_ID].as<Positive<uint32_t> >().value;
        }

        WalletAddress senderAddress = GenerateNewAddress(walletDB, "");
        auto txParams = lelantus::CreatePushTransactionParameters(senderAddress.m_walletID);
        LoadReceiverParams(vm, txParams);

        txParams.SetParameter(TxParameterID::Amount, amount)
                .SetParameter(TxParameterID::Fee, fee)
                .SetParameter(TxParameterID::AssetID, assetId)
                .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm));

        return wallet.StartTransaction(txParams);
    }

    int InsertToShieldedPool(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = InsertToShieldedPool(vm, *wallet, walletDB);
                return currentTxID ? 0: -1;
            });
    }

    boost::optional<TxID> ExtractFromShieldedPool(const po::variables_map& vm, Wallet& wallet, IWalletDB::Ptr walletDB)
    {
        TxoID shieldedId = 0;
        Amount fee = 0;

        if (!ReadFee(vm, fee, true) || !ReadShieldedId(vm, shieldedId))
        {
            return boost::none;
        }

        if (fee < kShieldedTxMinFeeInGroth)
        {
            LOG_ERROR () << "Fee is too small. Minimal fee is " << PrintableAmount(kShieldedTxMinFeeInGroth);
            return boost::none;
        }

        auto shieldedCoin = walletDB->getShieldedCoin(shieldedId);
        if (!shieldedCoin)
        {
            LOG_ERROR () << "Is not shielded UTXO id: " << shieldedId;
            return boost::none;
        }

        const bool isAsset = shieldedCoin->m_CoinID.m_AssetID != Asset::s_InvalidID;
        if (isAsset)
        {
            CheckAssetsAllowed(vm);
        }
        else
        {
            // BEAM
            if (shieldedCoin->m_CoinID.m_Value <= fee)
            {
                LOG_ERROR() << "Shielded UTXO amount less or equal fee.";
                return boost::none;
            }
        }

        WalletAddress senderAddress = GenerateNewAddress(walletDB, "");

        auto txParams = lelantus::CreatePullTransactionParameters(senderAddress.m_walletID)
            .SetParameter(TxParameterID::Amount, isAsset ? shieldedCoin->m_CoinID.m_Value : shieldedCoin->m_CoinID.m_Value - fee)
            .SetParameter(TxParameterID::Fee, fee)
            .SetParameter(TxParameterID::AssetID, shieldedCoin->m_CoinID.m_AssetID)
            .SetParameter(TxParameterID::ShieldedOutputId, shieldedId)
            .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm));

        return wallet.StartTransaction(txParams);
    }

    int ExtractFromShieldedPool(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {
                currentTxID = ExtractFromShieldedPool(vm, *wallet, walletDB);
                return currentTxID ? 0: -1;
            });
    }

}  // namespace

io::Reactor::Ptr reactor;

static const unsigned LOG_ROTATION_PERIOD_SEC = 3*60*60; // 3 hours

int main_impl(int argc, char* argv[])
{
    beam::Crash::InstallHandler(NULL);
    const Command commands[] =
    {
        {cli::INIT,               InitWallet,                       "initialize new wallet database with a new seed phrase"},
        {cli::RESTORE,            RestoreWallet,                    "restore wallet database from a seed phrase provided by the user"},
        {cli::SEND,               Send,                             "send BEAM"},
        {cli::LISTEN,             Listen,                           "listen to the node (the wallet won't close till halted"},
        {cli::TREASURY,           HandleTreasury,                   "process treasury"},
        {cli::INFO,               ShowWalletInfo,                   "print information about wallet balance and transactions"},
        {cli::EXPORT_MINER_KEY,   ExportMinerKey,                   "export miner key to pass to a mining node"},
        {cli::EXPORT_OWNER_KEY,   ExportOwnerKey,                   "export owner key to allow a node to monitor owned UTXO on the blockchain"},
        {cli::NEW_ADDRESS,        CreateNewAddress,                 "generate new SBBS address"},
        {cli::CANCEL_TX,          CancelTransaction,                "cancel transaction by ID"},
        {cli::DELETE_TX,          DeleteTransaction,                "delete transaction by ID"},
        {cli::CHANGE_ADDRESS_EXPIRATION, ChangeAddressExpiration,   "change SBBS address expiration time"},
        {cli::TX_DETAILS,         TxDetails,                        "print details of the transaction with given ID"},
        {cli::PAYMENT_PROOF_EXPORT, ExportPaymentProof,             "export payment proof by transaction ID"},
        {cli::PAYMENT_PROOF_VERIFY, VerifyPaymentProof,             "verify payment proof"},
        {cli::GENERATE_PHRASE,      GeneratePhrase,                 "generate new seed phrase"},
        {cli::WALLET_ADDRESS_LIST,  ShowAddressList,                "print SBBS addresses"},
        {cli::WALLET_RESCAN,        Rescan,                         "rescan the blockchain for owned UTXO (works only with node configured with an owner key)"},
        {cli::EXPORT_DATA,          ExportWalletData,               "export wallet data (UTXO, transactions, addresses) to a JSON file"},
        {cli::IMPORT_DATA,          ImportWalletData,               "import wallet data from a JSON file"},
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        {cli::SWAP_INIT,            InitSwap,                       "initialize atomic swap"},
        {cli::SWAP_ACCEPT,          AcceptSwap,                     "accept atomic swap offer"},
        {cli::SET_SWAP_SETTINGS,    SetSwapSettings,                "set generic atomic swap settings"},
        {cli::SHOW_SWAP_SETTINGS,   ShowSwapSettings,               "print BTC/LTC/QTUM-specific swap settings"},
#endif // BEAM_ATOMIC_SWAP_SUPPORT
        {cli::GET_TOKEN,            GetToken,                       "generate transaction token for a specific receiver (identifiable by SBBS address or wallet identity)"},
        {cli::SET_CONFIRMATIONS_COUNT, SetConfirmationsCount,       "set count of confirmations before you can't spend coin"},
        {cli::GET_CONFIRMATIONS_COUNT, GetConfirmationsCount,       "get count of confirmations before you can't spend coin"},
#ifdef BEAM_LASER_SUPPORT   
        {cli::LASER,                HandleLaser,                    "laser beam command"},
#endif  // BEAM_LASER_SUPPORT
        {cli::ASSET_ISSUE,          IssueAsset,                     "issue new confidential asset"},
        {cli::ASSET_CONSUME,        ConsumeAsset,                   "consume (burn) an existing confidential asset"},
        {cli::ASSET_REGISTER,       RegisterAsset,                  "register new asset with the blockchain"},
        {cli::ASSET_UNREGISTER,     UnregisterAsset,                "unregister asset from the blockchain"},
        {cli::ASSET_INFO,           GetAssetInfo,                   "print confidential asset information from a node"},
#ifdef BEAM_LELANTUS_SUPPORT
        // Basic lelantus operations are disabled in CLI starting from v5.1
        // {cli::INSERT_TO_POOL,       InsertToShieldedPool,           "insert UTXO to the shielded pool"},
        // {cli::EXTRACT_FROM_POOL,    ExtractFromShieldedPool,        "extract shielded UTXO from the shielded pool"}
#endif
    };

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
            printHelp(begin(commands), end(commands), visibleOptions);

            return 0;
        }

        if (vm.count(cli::HELP))
        {
            printHelp(begin(commands), end(commands), visibleOptions);

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
                    printHelp(begin(commands), end(commands), visibleOptions);
                    return 0;
                }

                auto command = vm[cli::COMMAND].as<string>();

                auto cit = find_if(begin(commands), end(commands), [&command](auto& p) {return p.name == command; });
                if (cit == end(commands))
                {
                    LOG_ERROR() << boost::format(kErrorCommandUnknown) % command;
                    return -1;
                }

                LOG_INFO() << boost::format(kVersionInfo) % PROJECT_VERSION % BRANCH_NAME;
                LOG_INFO() << boost::format(kRulesSignatureInfo) % Rules::get().get_SignatureStr();
                        
                return cit->handler(vm);
            }
        }
        catch (const ReceiverAddressExpiredException&)
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
            printHelp(begin(commands), end(commands), visibleOptions);
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
