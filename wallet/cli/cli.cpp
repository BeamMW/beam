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
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/transactions/swaps/common.h"
#include "wallet/transactions/swaps/utils.h"
#include "swaps.h"
#endif // BEAM_ATOMIC_SWAP_SUPPORT

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

#include "utils.h"

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

#include "bvm/ManagerStd.h"

using namespace std;
using namespace beam;
using namespace beam::wallet;
using namespace ECC;

namespace beam
{
    const char kElectrumSeparateSymbol = ' ';

    template<typename TStatusEnum>
    string getCoinStatus(TStatusEnum s)
    {
        stringstream ss;
        ss << "[";
        switch (s)
        {
        case TStatusEnum::Available:   ss << kCoinStatusAvailable; break;
        case TStatusEnum::Unavailable: ss << kCoinStatusUnavailable; break;
        case TStatusEnum::Spent:       ss << kCoinStatusSpent; break;
        case TStatusEnum::Maturing:    ss << kCoinStatusMaturing; break;
        case TStatusEnum::Outgoing:    ss << kCoinStatusOutgoing; break;
        case TStatusEnum::Incoming:    ss << kCoinStatusIncoming; break;
        case TStatusEnum::Consumed:    ss << kCoinStatusConsumed; break;
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

        auto assetsSICreator = [] (const TxParameters& txParams) {
            return std::make_shared<AssetTxStatusInterpreter>(txParams);
        };

        auto simpleSICreator = [] (const TxParameters& txParams) {
            return std::make_shared<TxStatusInterpreter>(txParams);
        };

        auto maxPrivSICreator = [] (const TxParameters& txParams) {
            return std::make_shared<MaxPrivacyTxStatusInterpreter>(txParams);
        };

        walletDB->addStatusInterpreterCreator(TxType::Simple,          simpleSICreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetIssue,      assetsSICreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetConsume,    assetsSICreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetReg,        assetsSICreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetUnreg,      assetsSICreator);
        walletDB->addStatusInterpreterCreator(TxType::AssetInfo,       assetsSICreator);
        walletDB->addStatusInterpreterCreator(TxType::PushTransaction, maxPrivSICreator);

        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
        walletDB->addStatusInterpreterCreator(TxType::AtomicSwap, [] (const TxParameters& txParams) {
            class CliSwapTxStatusInterpreter: public TxStatusInterpreter
            {
            public:
                explicit CliSwapTxStatusInterpreter(const TxParameters& txParams) : TxStatusInterpreter(txParams)
                {
                    if (auto value = txParams.GetParameter<AtomicSwapTransaction::State>(wallet::TxParameterID::State); value)
                        m_state = *value;
                }

                ~CliSwapTxStatusInterpreter() override = default;

                [[nodiscard]] std::string getStatus() const override
                {
                    return wallet::getSwapTxStatus(m_state);
                }
            private:
                wallet::AtomicSwapTransaction::State m_state = wallet::AtomicSwapTransaction::State::Initial;
            };
            return std::make_shared<CliSwapTxStatusInterpreter>(txParams);
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
        if (auto it = vm.find(cli::OFFLINE_ADDRESS); it != vm.end())
        {
            auto vouchers = GenerateVoucherList(db->get_KeyKeeper(), ownID, it->second.as<Positive<uint32_t>>().value);
            if (!vouchers.empty())
            {
                // add voucher parameter
                params.SetParameter(TxParameterID::ShieldedVoucherList, vouchers);
                params.SetParameter(TxParameterID::TransactionType, beam::wallet::TxType::PushTransaction);
            }
        }
    }

   int GetAddress(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        std::string newAddress;
        if (auto it2 = vm.find(cli::PUBLIC_OFFLINE); it2 != vm.end() && it2->second.as<bool>())
        {
            LOG_INFO() << "Generating public offline address";
            newAddress =  GeneratePublicOfflineAddress(*walletDB);
        }
        else if (it2 = vm.find(cli::MAX_PRIVACY_ADDRESS); it2 != vm.end() && it2->second.as<bool>())
        {
            LOG_INFO() << "Generating max privacy address";
            auto walletAddress = GenerateNewAddress(walletDB, "", WalletAddress::ExpirationStatus::Never);
            auto vouchers = GenerateVoucherList(walletDB->get_KeyKeeper(), walletAddress.m_OwnID, 1);
            newAddress = GenerateMaxPrivacyAddress(walletAddress, 0, vouchers[0], "");
        }
        else if (it2 = vm.find(cli::OFFLINE_ADDRESS); it2 != vm.end())
        {
            LOG_INFO() << "Generating offline address";
            auto walletAddress = GenerateNewAddress(walletDB, "", WalletAddress::ExpirationStatus::Never);
            auto vouchers = GenerateVoucherList(walletDB->get_KeyKeeper(), walletAddress.m_OwnID, it2->second.as<Positive<uint32_t>>().value);
            newAddress = GenerateOfflineAddress(walletAddress, 0, vouchers);
        }
        else
        {
            boost::optional<WalletAddress> address;
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
                address = walletDB->getAddress(walletID);
                if (!address)
                {
                    LOG_ERROR() << "Cannot get address, there is no SBBS";
                    return -1;
                }
                if (address->isExpired())
                {
                    LOG_ERROR() << "Cannot get address, it is expired";
                    return -1;
                }
                if (!address->isPermanent())
                {
                    LOG_ERROR() << "The address expiration time must be never.";
                    return -1;
                }
            }
            else
            {
                address = GenerateNewAddress(walletDB, "");
            }
            newAddress = GenerateRegularAddress(*address, 0, address->isPermanent(), "");
        }
        
        LOG_INFO() << "address: " << newAddress;
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
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldMaturing) % to_string(PrintableAmount(totals.Maturing, false, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldInProgress) % to_string(PrintableAmount(totals.Incoming, false, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldUnavailable) % to_string(PrintableAmount(totals.Unavail, false, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalUnspent) % to_string(PrintableAmount(totals.Unspent, false, unitName, nthName));
             // % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldShielded) % to_string(PrintableAmount(totals.Shielded, false, unitName, nthName));

        auto minHeight = std::min(totals.MinCoinHeightMW, totals.MinCoinHeightShielded);
        if (!info.is_initialized())
        {
            cout << kWalletNoInfo;
        }
        else if (minHeight && info->m_LockHeight > minHeight)
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

    void ShowAssetCoins(const IWalletDB::Ptr& walletDB, Asset::ID assetId, boost::optional<TxID> txID = {})
    {
        const auto [unitName, nthName] = GetAssetNames(walletDB, assetId);
        const uint8_t idWidth = assetId == Asset::s_InvalidID ? 49 : 57;
        const array<uint8_t, 6> columnWidths{{idWidth, 14, 14, 18, 20, 8}};

        const auto lockHeight = GetAssetLockHeight(walletDB, assetId);
        std::vector<boost::any> reliable;
        std::vector<boost::any> unreliable;

        auto isSkipedByTxID = [&](const auto& txID, const auto& createID, const auto& spentID)
        {
            if (!txID)
            {
                return false;
            }
            if (createID && *createID == *txID)
            {
                return false;
            }
            if (spentID && *spentID == *txID)
            {
                return false;
            }
            return true;
        };

        walletDB->visitCoins([&](const Coin& c)->bool {
            if (isSkipedByTxID(txID, c.m_createTxId, c.m_spentTxId))
            {
                // skip
                return true;
            }
            if (c.m_ID.m_AssetID == assetId)
            {
                if (c.m_confirmHeight < lockHeight)
                {
                    unreliable.emplace_back(c);
                }
                else
                {
                    reliable.emplace_back(c);
                }
            }
            return true;
        });

        walletDB->visitShieldedCoins([&](const ShieldedCoin& c)->bool {
            if (isSkipedByTxID(txID, c.m_createTxId, c.m_spentTxId))
            {
                // skip
                return true;
            }
            if (c.m_CoinID.m_AssetID == assetId)
            {
                if (c.m_confirmHeight < lockHeight)
                {
                    unreliable.emplace_back(c);
                }
                else
                {
                    reliable.emplace_back(c);
                }
            }
            return true;
        });

        auto getSortHeight = [&](boost::any& ca) -> Height {
            if (ca.type() == typeid(Coin))
            {
                const auto& c = boost::any_cast<Coin>(ca);
                return c.get_Maturity();
            }
            if(ca.type() == typeid(ShieldedCoin))
            {
                const auto &c = boost::any_cast<ShieldedCoin>(ca);
                return c.m_confirmHeight;
            }
            assert(false);
            return MaxHeight;
        };

        std::sort(reliable.begin(), reliable.end(), [&](boost::any& a, boost::any& b) {
            return getSortHeight(a) < getSortHeight(b);
        });

        auto offset = walletDB->getCoinConfirmationsOffset();
        const auto displayCoins = [&](const std::vector<boost::any>& coins) {
            if (coins.empty())
            {
                return;
            }
            for(const auto& ca: coins) {
                Amount     value = 0;
                std::string coinId;
                std::string coinStatus;
                std::string coinMaturity;
                std::string coinType;

                if (ca.type() == typeid(Coin))
                {
                    const auto& c = boost::any_cast<Coin>(ca);
                    value        = c.m_ID.m_Value;
                    coinId       = c.toStringID();
                    coinStatus   = getCoinStatus(c.m_status);
                    coinType     = FourCC::Text(c.m_ID.m_Type);
                    coinMaturity = c.IsMaturityValid() ? std::to_string(c.get_Maturity(offset)) : "-";
                }
                else if(ca.type() == typeid(ShieldedCoin))
                {
                    const auto& c = boost::any_cast<ShieldedCoin>(ca);
                    value         = c.m_CoinID.m_Value;
                    coinId        = c.m_TxoID == ShieldedCoin::kTxoInvalidID ? "--" : std::to_string(c.m_TxoID);
                    coinStatus    = getCoinStatus(c.m_Status);
                    coinType      = "shld";
                    coinMaturity  = c.IsMaturityValid() ? std::to_string(c.get_Maturity(offset)) : "-";
                }
                else
                {
                    assert(false); // this should never happen
                    continue;
                }

                cout << boost::format(kCoinsTableFormat)
                    % boost::io::group(right,setw(columnWidths[0]),  coinId)
                    % boost::io::group(right,setw(columnWidths[1]), value / Rules::Coin)
                    % boost::io::group(right,setw(columnWidths[2]), value % Rules::Coin)
                    % boost::io::group(left, setw(columnWidths[3]),  coinMaturity)
                    % boost::io::group(left, setw(columnWidths[4]),  coinStatus)
                    % boost::io::group(left, setw(columnWidths[5]),  coinType)
                  << std::endl;
            }
        };

        const bool hasCoins = !(reliable.empty() && unreliable.empty());
        if (hasCoins)
        {
            cout << boost::format(kCoinsTableHeadFormat)
                     % boost::io::group(right,setw(columnWidths[0]), kCoinColumnId)
                     % boost::io::group(right,setw(columnWidths[1]), unitName)
                     % boost::io::group(right,setw(columnWidths[2]), nthName)
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
        const bool show = vm.count(cli::TX_HISTORY);
        if (!show) return;

        std::vector<TxDescription> txHistory;

        if (vm.count(cli::TX_HISTORY))
        {
            auto txSimple  = walletDB->getTxHistory(TxType::Simple);
            auto txReg     = walletDB->getTxHistory(TxType::AssetReg);
            auto txIssue   = walletDB->getTxHistory(TxType::AssetIssue);
            auto txConsume = walletDB->getTxHistory(TxType::AssetConsume);
            auto txUnreg   = walletDB->getTxHistory(TxType::AssetUnreg);
            auto txInfo    = walletDB->getTxHistory(TxType::AssetInfo);
            auto txMaxPriv = walletDB->getTxHistory(TxType::PushTransaction);

            if (assetId != Asset::s_InvalidID)
            {
                txHistory.insert(txHistory.end(), txSimple.begin(), txSimple.end());
            }

            txHistory.insert(txHistory.end(), txReg.begin(), txReg.end());
            txHistory.insert(txHistory.end(), txIssue.begin(), txIssue.end());
            txHistory.insert(txHistory.end(), txConsume.begin(), txConsume.end());
            txHistory.insert(txHistory.end(), txUnreg.begin(), txUnreg.end());
            txHistory.insert(txHistory.end(), txInfo.begin(), txInfo.end());
            txHistory.insert(txHistory.end(), txMaxPriv.begin(), txMaxPriv.end());
        }

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

                const auto statusInterpreter = walletDB->getStatusInterpreter(tx);
                const auto status = statusInterpreter->getStatus();
                const auto tstamp = format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false);
                const auto txid   = to_hex(tx.m_txId.data(), tx.m_txId.size());

                cout << boost::format(kTxHistoryTableFormat)
                        % boost::io::group(left,  setw(columnWidths[0]),  tstamp)
                        % boost::io::group(left,  setw(columnWidths[1]),  static_cast<int64_t>(height))
                        % boost::io::group(left,  setw(columnWidths[2]),  direction)
                        % boost::io::group(right, setw(columnWidths[3]),  amount)
                        % boost::io::group(left,  setw(columnWidths[4]),  status)
                        % boost::io::group(left,  setw(columnWidths[5]),  txid)
                        % boost::io::group(left,  setw(columnWidths[6]),  kernelId)
                     << std::endl;
            }
        }
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
                filter(walletDB->getTxHistory(TxType::PushTransaction)); // max privacy
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

        auto avail    = totals.Avail;    avail    += totals.AvailShielded;
        auto unspent  = totals.Unspent;  unspent  += totals.UnspentShielded;
        auto maturing = totals.Maturing; maturing += totals.MaturingShielded;
        auto unavail  = totals.Unavail;  unavail  += totals.UnavailShielded;
        auto outgoing = totals.Outgoing; outgoing += totals.OutgoingShielded;
        auto incoming = totals.Incoming; incoming += totals.IncomingShielded;

        cout << boost::format(kWalletSummaryFormat)
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldCurHeight) % stateID.m_Height
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldCurStateID) % stateID.m_Hash
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailable) % to_string(PrintableAmount(avail))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldMaturing) % to_string(PrintableAmount(maturing))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldInProgress) % to_string(PrintableAmount(incoming))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldUnavailable) % to_string(PrintableAmount(unavail))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailableCoinbase) % to_string(PrintableAmount(totals.AvailCoinbase))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalCoinbase) % to_string(PrintableAmount(totals.Coinbase))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvaliableFee) % to_string(PrintableAmount(totals.AvailFee))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalFee) % to_string(PrintableAmount(totals.Fee))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalUnspent) % to_string(PrintableAmount(unspent));

        if (vm.count(cli::UTXO_LIST))
        {
            ShowAssetCoins(walletDB, Zero);
        }
        else if (vm.count(cli::TX_HISTORY) /*|| vm.count(cli::SHIELDED_TX_HISTORY)*/)
        {
            std::vector<TxDescription> txHistory;

            if (vm.count(cli::TX_HISTORY))
            {
                std::array<TxType, 3> types = { TxType::Simple, TxType::PushTransaction, TxType::Contract };
                for (auto type : types)
                {
                    auto v = walletDB->getTxHistory(type);
                    txHistory.insert(txHistory.end(), v.begin(), v.end());
                }
            }

            txHistory.erase(std::remove_if(txHistory.begin(), txHistory.end(), [](const auto& tx) {
                return tx.m_assetId != 0;
            }), txHistory.end());

            if (txHistory.empty())
            {
                cout << kTxHistoryEmpty << endl;
            }
            else
            {
                 std::sort(txHistory.begin(), txHistory.end(), [](const TxDescription& a, const TxDescription& b) -> bool {
                    return a.m_createTime > b.m_createTime;
                });

                const array<uint8_t, 7> columnWidths{ {20, 17, 26, 21, 33, 65, 100} };
                cout << boost::format(kTxHistoryTableHead)
                    % boost::io::group(left, setw(columnWidths[0]), kTxHistoryColumnDatetTime)
                    % boost::io::group(left, setw(columnWidths[1]), kTxHistoryColumnDirection)
                    % boost::io::group(right, setw(columnWidths[2]), kTxHistoryColumnAmount)
                    % boost::io::group(left, setw(columnWidths[3]), kTxHistoryColumnStatus)
                    % boost::io::group(left, setw(columnWidths[4]), kTxHistoryColumnId)
                    % boost::io::group(left, setw(columnWidths[5]), kTxHistoryColumnKernelId)
                    % boost::io::group(left, setw(columnWidths[6]), kTxAddress)
                    << std::endl;

                for (auto& tx : txHistory) {
                    const auto statusInterpreter = walletDB->getStatusInterpreter(tx);
                    const auto tstamp    = format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false);
                    const auto direction = tx.m_selfTx ? kTxDirectionSelf : (tx.m_sender ? kTxDirectionOut : kTxDirectionIn);
                    const auto amount    = to_string(PrintableAmount(tx.m_amount, true));
                    const auto status    = statusInterpreter->getStatus();
                    const auto txid      = to_hex(tx.m_txId.data(), tx.m_txId.size());
                    const auto krnid     = to_string(tx.m_kernelID);
                    const auto token     = tx.getToken();
                    cout << boost::format(kTxHistoryTableFormat)
                        % boost::io::group(left,  setw(columnWidths[0]), tstamp)
                        % boost::io::group(left,  setw(columnWidths[1]), direction)
                        % boost::io::group(right, setw(columnWidths[2]), amount)
                        % boost::io::group(left, setw(columnWidths[3]),  status)
                        % boost::io::group(left, setw(columnWidths[4]),  txid)
                        % boost::io::group(left, setw(columnWidths[5]),  krnid)
                        % boost::io::group(left, setw(columnWidths[6]),  token)
                        << std::endl;
                }
            }
        }
        else
        {
            std::cout << "If you wish to see the list of UXTOs or transaction history use\n\t--" << cli::UTXO_LIST << "  or  --" << cli::TX_HISTORY << "  parameters correspondingly" << std::endl;
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
                        % boost::io::group(left, setw(columnWidths[4]), statusInterpreter->getStatus())
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

        const auto txdetails = storage::TxDetailsInfo(walletDB, *txId);
        if (txdetails.empty()) {
            // storage::TxDetailsInfo already printed an error
            return -1;
        }

        const auto token = tx->getToken();
        const auto statusInterpreter = walletDB->getStatusInterpreter(*tx);
        const auto txstatus = statusInterpreter->getStatus();

        cout
            << "\n"
            << boost::format(kTxDetailsFormat) % txdetails % txstatus
            << (tx->m_status == TxStatus::Failed ? boost::format(kTxDetailsFailReason) % GetFailureMessage(tx->m_failureReason) : boost::format(""))
            << (!token.empty() ? "\nAddress:           " : "") << token;

        if (vm.count(cli::UTXO_LIST))
        {
            cout << "\n\n";
            ShowAssetCoins(walletDB, Zero, txId);
        }

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

    struct CliNodeConnection final : public proto::FlyClient::NetworkStd
    {
    public:
        explicit CliNodeConnection(proto::FlyClient& fc) : proto::FlyClient::NetworkStd(fc) {};
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
        TxFailureReason res = wallet::CheckAssetsEnabled(MaxHeight);
        if (TxFailureReason::Count != res)
            throw std::runtime_error(GetFailureMessage(res));
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

        auto txCompletedAction = isServer ? Wallet::TxCompletedAction() : onTxCompleteAction;

        auto wallet = std::make_shared<Wallet>(walletDB,
                      std::move(txCompletedAction),
                      Wallet::UpdateCompletedAction());
        {
            wallet::AsyncContextHolder holder(*wallet);

#ifdef BEAM_LELANTUS_SUPPORT
            lelantus::RegisterCreators(*wallet, walletDB);
#endif

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
            RegisterSwapTxCreators(wallet, walletDB);
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
#ifdef BEAM_CONFIDENTIAL_ASSETS_SUPPORT
           if (Rules::get().CA.Enabled && wallet::g_AssetsEnabled)
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
                Asset::ID assetId = Asset::s_BeamID;
                Amount amount = 0;
                Amount fee = 0;
                WalletID receiverWalletID(Zero);

                if (!LoadBaseParamsForTX(vm, assetId, amount, fee, receiverWalletID, isFork1))
                {
                    return -1;
                }

                if (assetId != Asset::s_BeamID)
                {
                    CheckAssetsAllowed(vm);
                }

                auto params = CreateSimpleTransactionParameters();
                LoadReceiverParams(vm, params);

                auto type = params.GetParameter<TxType>(TxParameterID::TransactionType);
                bool isPushTx = type && *type == TxType::PushTransaction;
                if (auto vouchers = params.GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList); vouchers)
                {
                    storage::SaveVouchers(*walletDB, *vouchers, receiverWalletID);
                }

                if (auto voucher = params.GetParameter<ShieldedTxo::Voucher>(TxParameterID::Voucher); voucher)
                {
                    params.SetParameter(TxParameterID::MaxPrivacyMinAnonimitySet, uint8_t(64));
                }

                Amount feeForShieldedInputs = 0;
                if (!CheckFeeForShieldedInputs(amount, fee, assetId, walletDB, isPushTx, feeForShieldedInputs))
                    return -1;

                if (isPushTx)
                {
                    const auto& ownAddresses = walletDB->getAddresses(true);
                    auto it = std::find_if(
                        ownAddresses.begin(), ownAddresses.end(),
                        [&receiverWalletID] (const WalletAddress& addr)
                        {
                            return receiverWalletID == addr.m_walletID;
                        });

                    if (it != ownAddresses.end())
                    {
                        LOG_ERROR() << kErrorCantSendMaxPrivacyToOwn;
                        return -1;
                    }
                }


                WalletAddress senderAddress = GenerateNewAddress(walletDB, "");
                params.SetParameter(TxParameterID::MyID, senderAddress.m_walletID)
                    .SetParameter(TxParameterID::Amount, amount)
                    // fee for shielded inputs included automaticaly
                    .SetParameter(TxParameterID::Fee, fee - feeForShieldedInputs)
                    .SetParameter(TxParameterID::AssetID, assetId)
                    .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm));
                currentTxID = wallet->StartTransaction(params);

                return 0;
            });
    }

    int ShaderInvoke(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](const po::variables_map& vm, auto&& wallet, auto&& walletDB, auto& currentTxID, bool isFork1)
            {

			    struct MyManager
				    :public bvm2::ManagerStd
			    {
				    bool m_Done = false;
				    bool m_Err = false;
                    bool m_Async = false;

				    void OnDone(const std::exception* pExc) override
				    {
					    m_Done = true;
					    m_Err = !!pExc;

                        if (pExc)
                            std::cout << "Shader exec error: " << pExc->what() << std::endl;
                        else
                            std::cout << "Shader output: " << m_Out.str() << std::endl;

                        if (m_Async)
                            io::Reactor::get_Current().stop();
				    }

                    static void Compile(ByteBuffer& res, const char* sz, Kind kind)
                    {
                        std::FStream fs;
                        fs.Open(sz, true, true);

                        res.resize(static_cast<size_t>(fs.get_Remaining()));
                        if (!res.empty())
                            fs.read(&res.front(), res.size());

                        bvm2::Processor::Compile(res, res, kind);
                    }
                };

                MyManager man;
                man.m_pPKdf = walletDB->get_OwnerKdf();
                man.m_pNetwork = wallet->GetNodeEndpoint();
                man.m_pHist = &walletDB->get_History();

                auto sVal = vm[cli::SHADER_BYTECODE_MANAGER].as<string>();
                if (sVal.empty())
                    throw std::runtime_error("shader file not specified");

                MyManager::Compile(man.m_BodyManager, sVal.c_str(), MyManager::Kind::Manager);

                sVal = vm[cli::SHADER_BYTECODE_CONTRACT].as<string>();
                if (!sVal.empty())
                    MyManager::Compile(man.m_BodyContract, sVal.c_str(), MyManager::Kind::Contract);

                sVal = vm[cli::SHADER_ARGS].as<string>(); // should be comma-separated list of name=val pairs
                if (!sVal.empty())
                    man.AddArgs(&sVal.front());
               
                std::cout << "Executing shader..." << std::endl;

                man.StartRun(man.m_Args.empty() ? 0 : 1); // scheme if no args

                if (!man.m_Done)
                {
                    man.m_Async = true;
                    io::Reactor::get_Current().run();

                    if (!man.m_Done)
                    {
                        // abort, propagate it
                        io::Reactor::get_Current().stop();
                        return -1;
                    }
                }

                if (man.m_Err || man.m_vInvokeData.empty())
                    return 1;

                std::cout << "Creating new contract invocation tx on behalf of the shader" << std::endl;

                currentTxID = wallet->StartTransaction(
                    CreateTransactionParameters(TxType::Contract)
                    .SetParameter(TxParameterID::ContractDataPacked, man.m_vInvokeData));
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
                    LOG_ERROR() << kErrorCancelTxInInvalidStatus << statusInterpreter->getStatus();
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

            return SetSwapSettings(vm, walletDB, swapCoin);
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
            
            ShowSwapSettings(vm, walletDB, swapCoin);
            return 0;
        }

        LOG_ERROR() << "swap_coin should be specified";
        return -1;
    }

    int EstimateSwapFeerate(const po::variables_map& vm)
    {
        if (vm.count(cli::SWAP_COIN) > 0)
        {
            auto walletDB = OpenDataBase(vm);
            auto swapCoin = wallet::from_string(vm[cli::SWAP_COIN].as<string>());
            Amount feeRate = EstimateSwapFeerate(swapCoin, walletDB);

            cout << "estimate fee rate = " << feeRate;
            return 0;
        }

        LOG_ERROR() << "swap_coin should be specified";
        return -1;
    }

    int GetBalance(const po::variables_map& vm)
    {
        if (vm.count(cli::SWAP_COIN) > 0)
        {
            auto walletDB = OpenDataBase(vm);
            auto swapCoin = wallet::from_string(vm[cli::SWAP_COIN].as<string>());
            Amount balance = GetBalance(swapCoin, walletDB);

            cout << "avaible: " << balance;
            return 0;
        }

        LOG_ERROR() << "swap_coin should be specified";
        return -1;
    }
#endif // BEAM_ATOMIC_SWAP_SUPPORT

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
        {cli::SHADER_INVOKE,      ShaderInvoke,                     "Invoke a wallet-side shader"},
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
        {cli::ESTIMATE_SWAP_FEERATE, EstimateSwapFeerate,           "estimate BTC/LTC/QTUM-specific fee rate"},
        {cli::GET_BALANCE,          GetBalance,                     "get BTC/LTC/QTUM balance"},
#endif // BEAM_ATOMIC_SWAP_SUPPORT
        {cli::GET_ADDRESS,            GetAddress,                   "generate transaction address for a specific receiver (identifiable by SBBS address or wallet identity)"},
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

            wallet::g_AssetsEnabled = vm[cli::WITH_ASSETS].as<bool>();


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

                auto cit = find_if(begin(commands), end(commands), [&command](const auto& p) {return p.name == command; });
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
