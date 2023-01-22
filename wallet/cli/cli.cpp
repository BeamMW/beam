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
#include "wallet/core/contracts/shaders_manager.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/transactions/swaps/common.h"
#include "wallet/transactions/swaps/utils.h"
#include "wallet/transactions/swaps/swap_tx_description.h"
#include "swaps.h"
#endif // BEAM_ATOMIC_SWAP_SUPPORT

#include "wallet/transactions/assets/assets_reg_creators.h"
#include "keykeeper/local_private_key_keeper.h"
#include "keykeeper/hid_key_keeper.h"
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "core/treasury.h"
#include "core/block_rw.h"
#include <algorithm>
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

#ifdef BEAM_ASSET_SWAP_SUPPORT
#include "assets_swap.h"
#endif  // BEAM_ASSET_SWAP_SUPPORT

#include <boost/assert.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/erase.hpp>

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

    void trimAssetName(std::string& name, int maxLen = 12)
    {
        auto len = static_cast<int>(name.length());
        if (len > maxLen)
        {
            boost::algorithm::erase_tail(name, len - maxLen);
            name.append("...");
        }
    }

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
    std::string interpretStatusCliImpl(const beam::wallet::TxDescription& tx)
    {
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        AtomicSwapTransaction::State state = wallet::AtomicSwapTransaction::State::Initial;
        if (tx.m_txType == TxType::AtomicSwap)
        {
            if (auto value = tx.GetParameter<AtomicSwapTransaction::State>(wallet::TxParameterID::State); value)
                state = *value;
            return wallet::getSwapTxStatus(state);
        }
        else
        {
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
            return beam::wallet::interpretStatus(tx);
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
    }

    std::string TxDetailsInfo(const IWalletDB::Ptr& walletDB, const TxID& txID)
    {
        auto tx = walletDB->getTx(txID);
        if (!tx)
        {
            LOG_WARNING() << "Can't get transaction details";
            return "";
        }
        const TxDescription& desc = *tx;
        TxAddressType addressType = GetAddressType(desc);

        if (addressType == TxAddressType::AtomicSwap)
        {
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
            Amount swapAmount = 0u;
            AtomicSwapCoin swapCoin = AtomicSwapCoin::Bitcoin;
            bool isBeamSide = true;
            bool bSuccess = storage::getTxParameter(*walletDB, txID, TxParameterID::AtomicSwapAmount, swapAmount);
            bSuccess = bSuccess && storage::getTxParameter(*walletDB, txID, TxParameterID::AtomicSwapCoin, swapCoin);
            bSuccess = bSuccess && storage::getTxParameter(*walletDB, txID, TxParameterID::AtomicSwapIsBeamSide, isBeamSide);

            if (bSuccess)
            {
                SwapTxDescription swapDescription(desc);
                auto swapToken = swapDescription.getToken();

                if (swapToken)
                {
                    std::ostringstream s;

                    s << "Type:              " << "atomic swap" << std::endl;
                    s << "Swap coin:         " << std::to_string(swapCoin) << std::endl;
                    s << "Beam side:         " << isBeamSide << std::endl;
                    s << "Beam amount:       " << PrintableAmount(desc.m_amount) << std::endl;
                    s << "Swap amount:       " << std::to_string(swapAmount) << std::endl;
                    s << "Swap token:        " << *swapToken << std::endl;

                    return s.str();
                }
            }
#endif // BEAM_ATOMIC_SWAP_SUPPORT
            return "";
        }

        bool hasNoPeerId = desc.m_sender && (addressType == TxAddressType::PublicOffline || addressType == TxAddressType::MaxPrivacy);

        auto senderIdentity = desc.getSenderIdentity();
        auto receiverIdentity = desc.getReceiverIdentity();
        bool showIdentity = !senderIdentity.empty() && !receiverIdentity.empty();

        auto sender = desc.getSender();
        auto receiver = desc.getReceiver();
        if (desc.m_txType == wallet::TxType::PushTransaction && !desc.m_sender)
        {
            sender = "shielded pool";
        }
        else if (desc.m_txType == wallet::TxType::Contract)
        {
            sender = receiver = "n/a";
        }

        std::ostringstream s;
        s << "Type:              " << desc.getTxTypeString() << '\n';
        s << "Sender:            " << sender << std::endl;
        if (showIdentity)
        {
            s << "Sender wallet's signature:   " << senderIdentity << std::endl;
        }

        s << "Receiver:          " << (hasNoPeerId ? desc.getToken() : receiver) << std::endl;
        if (showIdentity)
        {
            s << "Receiver wallet's signature: " << receiverIdentity << std::endl;
        }

        if (desc.m_assetId == Asset::s_BeamID)
        {
            s << "Amount:            " << PrintableAmount(desc.m_amount) << std::endl;
        }
        else
        {
            const auto info = walletDB->findAsset(desc.m_assetId);
            std::string unitName = kAmountASSET;
            std::string nthName  = kAmountAGROTH;
            if (info.is_initialized())
            {
                const auto &meta = WalletAssetMeta(*info);
                unitName = meta.GetUnitName();
                trimAssetName(unitName, 28);
                nthName  = meta.GetNthUnitName();
                trimAssetName(nthName, 28);
            }
            s << "Amount:            "
              << PrintableAmount(desc.m_amount, false, desc.m_assetId, unitName, nthName) << std::endl;
        }

        s << "KernelID:          " << std::to_string(desc.m_kernelID) << std::endl;

        return s.str();

    }

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

        IWalletDB::Ptr walletDB = WalletDB::open(walletPath, pass);

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
        string token = vm[cli::WALLET_ADDR].as<string>();
        string expiration = vm[cli::EXPIRATION_TIME].as<string>();

        WalletAddress::ExpirationStatus expirationStatus;
        if (expiration == cli::EXPIRATION_TIME_24H)
        {
            expirationStatus = WalletAddress::ExpirationStatus::OneDay;
        }
        else if (expiration == cli::EXPIRATION_TIME_AUTO)
        {
            expirationStatus = WalletAddress::ExpirationStatus::Auto;
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
        if (token == "*")
        {
            WalletID zeroID(Zero);
            if (storage::changeAddressExpiration(*walletDB, zeroID, expirationStatus))
            {
                std::cout << boost::format(kAllAddrExprChanged) % expiration << endl;
            }
        }
        else
        {
            if (auto address = walletDB->getAddressByToken(token, false))
            {
                address->setExpirationStatus(expirationStatus);
                walletDB->saveAddress(*address);

                std::cout << boost::format(kAddrExprChanged) % token % expiration << endl;
            }
            else
            {
                throw std::runtime_error("Cannot find specified existing address.");
            }
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
        else if (expiration == cli::EXPIRATION_TIME_AUTO)
        {
            expirationStatus = WalletAddress::ExpirationStatus::Auto;
        }
        else if (expiration == cli::EXPIRATION_TIME_NEVER)
        {
            expirationStatus = WalletAddress::ExpirationStatus::Never;
        }
        else
        {
            std::cerr << boost::format(kErrorAddrExprTimeInvalid) % cli::EXPIRATION_TIME % expiration;
            return false;
        }

        WalletAddress address(comment);
        walletDB->createAddress(address);
        address.setExpirationStatus(expirationStatus);
        walletDB->saveAddress(address);

        std::cout << "New SBBS address: " << std::to_string(address.m_walletID);
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

    enum struct InitKind {
        GenerateSeed,
        RecoverFromSeed,
        RecoverFromUsb,
    };

    int InitDataBase(const po::variables_map& vm, InitKind kind)
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

        IWalletDB::Ptr walletDB;

        if (InitKind::RecoverFromUsb == kind)
            walletDB = WalletDB::initHww(walletPath, pass);
        else
        {
            NoLeak<uintBig> walletSeed;
            walletSeed.V = Zero;
            if (!ReadWalletSeed(walletSeed, vm, InitKind::GenerateSeed == kind))
            {
                LOG_ERROR() << kErrorSeedPhraseFail;
                return -1;
            }

            walletDB = WalletDB::init(walletPath, pass, walletSeed);
        }


        if (walletDB)
        {
            LOG_INFO() << kWalletCreatedMessage;
            walletDB->generateAndSaveDefaultAddress();
            return 0;
        }
        LOG_ERROR() << kErrorWalletNotCreated;
        return -1;
    }

    int EnumUsb(const po::variables_map& vm)
    {
        auto vRes = wallet::HidInfo::EnumSupported();
        if (vRes.empty())
            std::cout << "No supported USB devices found" << std::endl;
        else
        {
            std::cout << "Found devices: " << vRes.size() << std::endl;

            for (const auto& x : vRes)
                std::cout << "\tManufacturer: " << x.m_sManufacturer << ", Name: " << x.m_sProduct << std::endl;
        }

        return 0;
    }

    int InitWallet(const po::variables_map& vm)
    {
        return InitDataBase(vm, InitKind::GenerateSeed);
    }

    int RestoreWallet(const po::variables_map& vm)
    {
        return InitDataBase(vm, InitKind::RecoverFromSeed);
    }

    int RestoreWalletUsb(const po::variables_map& vm)
    {
        return InitDataBase(vm, InitKind::RecoverFromUsb);
    }

   int GetAddress(const po::variables_map& vm)
   {
        auto walletDB = OpenDataBase(vm);

        auto type = TokenType::RegularNewStyle;
        uint32_t offlineCount = 10;

        WalletAddress address;
        walletDB->createAddress(address);

        if (auto it2 = vm.find(cli::PUBLIC_OFFLINE); it2 != vm.end() && it2->second.as<bool>())
        {
            type = TokenType::Public;
        }
        else if (it2 = vm.find(cli::MAX_PRIVACY_ADDRESS); it2 != vm.end() && it2->second.as<bool>())
        {
            type = TokenType::MaxPrivacy;
        }
        else if (it2 = vm.find(cli::OFFLINE_COUNT); it2 != vm.end())
        {
            type = TokenType::Offline;
            offlineCount = it2->second.as<Positive<uint32_t>>().value;
        }
        else
        {
            if (auto it = vm.find(cli::RECEIVER_ADDR); it != vm.end())
            {
                auto receiver = it->second.as<string>();

                WalletID walletID;
                if (!walletID.FromHex(receiver))
                {
                    throw std::runtime_error("Invalid existing address format");
                }

                auto existing = walletDB->getAddress(walletID);

                if (!existing)
                {
                    throw std::runtime_error("Cannot find specified existing address.");
                }

                if (existing->isExpired())
                {
                    throw std::runtime_error("Specified existing address is already expired");
                }

                address = *existing;
            }
        }

        try
        {
            address.m_Address = GenerateToken(type, address, walletDB, offlineCount);
            walletDB->saveAddress(address);
            std::cout << "New address: " << address.m_Address << std::endl;
        }
        catch (const std::exception& ex)
        {
            std::cerr << ex.what();
        }

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
        auto phrase = createMnemonic(getEntropy());
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
                || (vm.count(cli::IGNORE_DICTIONARY) == 0 && !isValidMnemonic(phrase)))
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

    std::string GetAddressTypeString(const WalletAddress& address)
    {
        const auto type = GetTokenType(address.m_Address);
        switch (type)
        {
            case TokenType::Public: return "public offline";
            case TokenType::MaxPrivacy: return "max privacy";
            case TokenType::RegularNewStyle: return "regular new style";
            case TokenType::RegularOldStyle: return "regular old style";
            case TokenType::Offline: return "offline";
            default: return "unknown";
        }
    }

    int ShowAddressList(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);
        auto addresses = walletDB->getAddresses(true);

        if (addresses.empty())
        {
            std::cout << "You do not have any addresses.";
            return 0;
        }

        std::cout << endl;
        for (const auto& address : addresses)
        {
            std::cout
                << kAddrListType    << GetAddressTypeString(address) << std::endl
                << kAddrListComment << address.m_label << std::endl
                << kAddrListAddress << address.m_Address << std::endl;

            if (address.m_walletID.IsValid())
            {
                std::cout << kAddrListWalletID << std::to_string(address.m_walletID) << std::endl;
            }

            const auto expirationDateText = (address.m_duration == 0)
                ? cli::EXPIRATION_TIME_NEVER
                : format_timestamp(kTimeStampFormat3x3, address.getExpirationTime() * 1000, false);

            const auto creationDateText = format_timestamp(kTimeStampFormat3x3, address.getCreateTime() * 1000, false);

            std::cout
                << kAddrListIdentity << std::to_string(address.m_Identity) << std::endl
                << kAddrListActive   << (address.isExpired() ? "false" : "true") << std::endl
                << kAddrListExprDate << expirationDateText << std::endl
                << kAddrListCreated  << creationDateText << std::endl
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
            const WalletAssetMeta &meta = WalletAssetMeta(*info);
            isOwned  = info->m_IsOwned;
            unitName = meta.GetUnitName();
            trimAssetName(unitName, 28);
            nthName  = meta.GetNthUnitName();
            trimAssetName(nthName, 28);
            ownerStr = (isOwned ? info->m_Owner.str() + "\nYou own this asset": info->m_Owner.str());
            coinName = meta.GetName() + " (" + meta.GetShortName() + ")";
            trimAssetName(coinName, 88);
            lkHeight = std::to_string(info->m_LockHeight);
            rfHeight = std::to_string(info->m_RefreshHeight);

            std::stringstream ss;

            ss << PrintableAmount(info->m_Value, true, totals.AssetId, unitName, nthName);
            emission = ss.str();
        }

        beam::AmountBig::Type available = totals.Avail;
        available += totals.AvailShielded;

        beam::AmountBig::Type unspent = totals.Unspent;
        unspent += totals.UnspentShielded;

        beam::AmountBig::Type maturing = totals.Maturing;
        maturing += totals.MaturingShielded;

        const unsigned kWidth = 26;
        cout << boost::format(kWalletAssetSummaryFormat)
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetIDFormat) % totals.AssetId
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetNameFormat) % coinName
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetLockHeightFormat) % lkHeight
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetRefreshHeightFormat) % rfHeight
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetEmissionFormat) % emission
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletAssetOwnerFormat) % ownerStr
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailable) % to_string(PrintableAmount(available, false, totals.AssetId, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldMaturing) % to_string(PrintableAmount(maturing, false, totals.AssetId, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldInProgress) % to_string(PrintableAmount(totals.Incoming, false, totals.AssetId, unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldUnavailable) % to_string(PrintableAmount(totals.Unavail, false, totals.AssetId,unitName, nthName))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalUnspent) % to_string(PrintableAmount(unspent, false, totals.AssetId, unitName, nthName));
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
        auto [unitName, nthName] = GetAssetNames(walletDB, assetId);
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
                    coinMaturity = c.IsMaturityValid() ? std::to_string(c.get_Maturity()) : "-";
                }
                else if(ca.type() == typeid(ShieldedCoin))
                {
                    const auto& c = boost::any_cast<ShieldedCoin>(ca);
                    value         = c.m_CoinID.m_Value;
                    coinId        = c.m_TxoID == ShieldedCoin::kTxoInvalidID ? "--" : std::to_string(c.m_TxoID);
                    coinStatus    = getCoinStatus(c.m_Status);
                    coinType      = "shld";
                    coinMaturity  = c.IsMaturityValid() ? std::to_string(c.get_Maturity()) : "-";
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
            trimAssetName(unitName, 12);
            trimAssetName(nthName, 12);
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
            auto txReg     = walletDB->getTxHistory(TxType::AssetReg);
            auto txIssue   = walletDB->getTxHistory(TxType::AssetIssue);
            auto txConsume = walletDB->getTxHistory(TxType::AssetConsume);
            auto txUnreg   = walletDB->getTxHistory(TxType::AssetUnreg);
            auto txInfo    = walletDB->getTxHistory(TxType::AssetInfo);
            auto txMaxPriv = walletDB->getTxHistory(TxType::PushTransaction);
#ifdef BEAM_ASSET_SWAP_SUPPORT
            auto txAssetsSwaps = walletDB->getTxHistory(TxType::DexSimpleSwap);
#endif  // BEAM_ASSET_SWAP_SUPPORT

            if (assetId != Asset::s_InvalidID)
            {
                auto txSimple = walletDB->getTxHistory(TxType::Simple);
                txHistory.insert(txHistory.end(), txSimple.begin(), txSimple.end());
            }

            txHistory.insert(txHistory.end(), txReg.begin(), txReg.end());
            txHistory.insert(txHistory.end(), txIssue.begin(), txIssue.end());
            txHistory.insert(txHistory.end(), txConsume.begin(), txConsume.end());
            txHistory.insert(txHistory.end(), txUnreg.begin(), txUnreg.end());
            txHistory.insert(txHistory.end(), txInfo.begin(), txInfo.end());
            txHistory.insert(txHistory.end(), txMaxPriv.begin(), txMaxPriv.end());
#ifdef BEAM_ASSET_SWAP_SUPPORT
            txHistory.insert(txHistory.end(), txAssetsSwaps.begin(), txAssetsSwaps.end());
#endif  // BEAM_ASSET_SWAP_SUPPORT
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
            trimAssetName(unitName, 7);
            const auto amountHeader = boost::format(kAssetTxHistoryColumnAmount) %  unitName;

            cout << "TRANSACTIONS" << std::endl << std::endl << std::string(120, '-') << std::endl;

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

                const auto tstamp = format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false);
                const auto txid   = to_hex(tx.m_txId.data(), tx.m_txId.size());
                const auto token     = tx.getToken();

                cout << std::string(4, ' ') << kTxHistoryColumnDatetTime << ": " << tstamp << std::endl;
                cout << std::string(4, ' ') <<  kTxHistoryColumnHeight << ": " << static_cast<int64_t>(height) << std::endl;
#ifdef BEAM_ASSET_SWAP_SUPPORT
                if (tx.m_txType == TxType::DexSimpleSwap)
                {
                    cout << std::string(4, ' ') << "asset swap: true" << std::endl;
                }
#endif  // BEAM_ASSET_SWAP_SUPPORT
                cout << std::string(4, ' ') << kTxHistoryColumnDirection << ": " << direction << std::endl;
                cout << std::string(4, ' ') << amountHeader << ": " << amount << std::endl;
#ifdef BEAM_ASSET_SWAP_SUPPORT
                if (tx.m_txType == TxType::DexSimpleSwap)
                {
                    const auto rasset  = tx.GetParameter<Asset::ID>(TxParameterID::DexReceiveAsset);
                    const auto ramount = tx.GetParameter<Amount>(TxParameterID::DexReceiveAmount);
                    auto [unitNameSecond, nthNameSekond] = GetAssetNames(walletDB, *rasset);
                    boost::ignore_unused(nthNameSekond);
                    const auto amountSecondHeader = boost::format(kAssetTxHistoryColumnAmount) %  unitNameSecond;
                    std::string amountSecond = to_string(PrintableAmount(*ramount, true));
                    cout << std::string(4, ' ') << amountSecondHeader << ": " << amountSecond << std::endl;
                }
#endif  // BEAM_ASSET_SWAP_SUPPORT
                cout << std::string(4, ' ') <<  kTxHistoryColumnStatus << ": " << interpretStatusCliImpl(tx) << std::endl;
                cout << std::string(4, ' ') << kTxHistoryColumnId << ": " << txid << std::endl;
                if (!kernelId.empty())
                    cout << std::string(4, ' ') <<  kTxHistoryColumnKernelId << ": " << kernelId << std::endl;
                if (!token.empty())
                    cout << std::string(4, ' ') << kTxAddress << ": " << token << std::endl;
                cout << std::string(120, '-') << std::endl;
            }
        }
    }

    void ShowAssetsInfo(const po::variables_map& vm, IWalletDB::Ptr walletDB)
    {
        if (!walletDB)
        {
            walletDB = OpenDataBase(vm);
        }

        auto showAssetId = Asset::s_InvalidID;
        if (vm.count(cli::ASSET_ID))
        {
            showAssetId = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
        }

        Block::SystemState::ID stateID = {};
        walletDB->getSystemStateID(stateID);
        storage::Totals totals(*walletDB, false);

        const auto displayAsset = [&vm, &walletDB](const storage::Totals::AssetTotals &totals) {
            cout << endl;
            ShowAssetInfo(walletDB, totals);

            if (vm.count(cli::UTXO_LIST))
            {
                ShowAssetCoins(walletDB, totals.AssetId);
            }

            ShowAssetTxs(vm, walletDB, totals.AssetId);
        };

        if (showAssetId != Asset::s_InvalidID)
        {
            displayAsset(totals.GetTotals(showAssetId));
        }
        else
        {
            bool assetDisplayed = false;
            for (auto it : totals.GetAllTotals())
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

    void ShowBEAMInfo(const po::variables_map& vm, IWalletDB::Ptr walletDB)
    {
        if (!walletDB)
        {
            walletDB = OpenDataBase(vm);
        }

        Block::SystemState::ID stateID = {};
        walletDB->getSystemStateID(stateID);
        storage::Totals totalsCalc(*walletDB, false);

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
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailable) % to_string(PrintableAmount(avail, false, 0))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldMaturing) % to_string(PrintableAmount(maturing, false, 0))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldInProgress) % to_string(PrintableAmount(incoming, false, 0))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldUnavailable) % to_string(PrintableAmount(unavail, false, 0))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvailableCoinbase) % to_string(PrintableAmount(totals.AvailCoinbase, false, 0))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalCoinbase) % to_string(PrintableAmount(totals.Coinbase, false, 0))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldAvaliableFee) % to_string(PrintableAmount(totals.AvailFee, false, 0))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalFee) % to_string(PrintableAmount(totals.Fee, false, 0))
             % boost::io::group(left, setfill('.'), setw(kWidth), kWalletSummaryFieldTotalUnspent) % to_string(PrintableAmount(unspent, false, 0));

        if (vm.count(cli::UTXO_LIST))
        {
            ShowAssetCoins(walletDB, Zero);
        }
        else if (vm.count(cli::TX_HISTORY))
        {
            std::vector<TxDescription> txHistory;

            if (vm.count(cli::TX_HISTORY))
            {
#ifdef BEAM_ASSET_SWAP_SUPPORT
                std::array<TxType, 4> types = { TxType::Simple, TxType::PushTransaction, TxType::Contract, TxType::DexSimpleSwap };
#else
                std::array<TxType, 3> types = { TxType::Simple, TxType::PushTransaction, TxType::Contract };
#endif  // BEAM_ASSET_SWAP_SUPPORT
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

                cout << "TRANSACTIONS" << std::endl << std::endl << std::string(120, '-') << std::endl;

                for (auto& tx : txHistory) {
                    const auto tstamp    = format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false);
                    const auto direction = tx.m_selfTx ? kTxDirectionSelf : (tx.m_sender ? kTxDirectionOut : kTxDirectionIn);
                    const auto amount    = to_string(PrintableAmount(tx.m_amount, true));
                    const auto txid      = to_hex(tx.m_txId.data(), tx.m_txId.size());
                    const auto krnid     = to_string(tx.m_kernelID);
                    const auto token     = tx.getToken();

                    cout << std::string(4, ' ') << kTxHistoryColumnDatetTime << ": " << tstamp << std::endl;
                    #ifdef BEAM_ASSET_SWAP_SUPPORT
                    if (tx.m_txType == TxType::DexSimpleSwap)
                    {
                        cout << std::string(4, ' ') << "asset swap: true" << std::endl;
                    }
#endif  // BEAM_ASSET_SWAP_SUPPORT
                    cout << std::string(4, ' ') << kTxHistoryColumnDirection << ": " << direction << std::endl;
                    cout << std::string(4, ' ') << kTxHistoryColumnAmount << ": " << amount << std::endl;
                    #ifdef BEAM_ASSET_SWAP_SUPPORT
                    if (tx.m_txType == TxType::DexSimpleSwap)
                    {
                        const auto rasset  = tx.GetParameter<Asset::ID>(TxParameterID::DexReceiveAsset);
                        const auto ramount = tx.GetParameter<Amount>(TxParameterID::DexReceiveAmount);
                        auto [unitNameSecond, nthNameSekond] = GetAssetNames(walletDB, *rasset);
                        boost::ignore_unused(nthNameSekond);
                        const auto amountSecondHeader = boost::format(kAssetTxHistoryColumnAmount) %  unitNameSecond;
                        std::string amountSecond = to_string(PrintableAmount(*ramount, true));
                        cout << std::string(4, ' ') << amountSecondHeader << ": " << amountSecond << std::endl;
                    }
#endif  // BEAM_ASSET_SWAP_SUPPORT
                    cout << std::string(4, ' ') << kTxHistoryColumnStatus << ": " << interpretStatusCliImpl(tx) << std::endl;
                    cout << std::string(4, ' ') << kTxHistoryColumnId << ": " << txid << std::endl;
                    if (!krnid.empty())
                        cout << std::string(4, ' ') << kTxHistoryColumnKernelId << ": " << krnid << std::endl;
                    if (!token.empty())
                        cout << std::string(4, ' ') << kTxAddress << ": " << token << std::endl;
                    cout << std::string(120, '-') << std::endl;
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

                    cout << boost::format(kSwapTxHistoryTableFormat)
                        % boost::io::group(left, setw(columnWidths[0]),
                            format_timestamp(kTimeStampFormat3x3, tx.m_createTime * 1000, false))
                        % boost::io::group(right, setw(columnWidths[1]),
                            to_string(PrintableAmount(tx.m_amount, true)))
                        % boost::io::group(right, setw(columnWidths[2]), swapAmount)
                        % boost::io::group(right, setw(columnWidths[3]), ss.str())
                        % boost::io::group(left, setw(columnWidths[4]), interpretStatusCliImpl(tx))
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
        const bool assetsOnly = vm.count(cli::ASSET_ID) != 0;
        IWalletDB::Ptr wdb = OpenDataBase(vm);

        if (assetsOnly)
        {
            ShowAssetsInfo(vm, wdb);
        }
        else
        {
            ShowBEAMInfo(vm, wdb);
            ShowAssetsInfo(vm, wdb);
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

        const auto txdetails = TxDetailsInfo(walletDB, *txId);
        if (txdetails.empty()) {
            // TxDetailsInfo already printed an error
            return -1;
        }

        const auto token = tx->getToken();

        cout
            << "\n"
            << boost::format(kTxDetailsFormat) % txdetails % interpretStatusCliImpl(*tx)
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
            std::cout << boost::format(kPpExportedFrom) % sTxt;
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
        auto walletDB = OpenDataBase(vm);

        try
        {
            isValid = storage::VerifyPaymentProof(buf, *walletDB);
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
        if (vm.count(cli::KEY_SUBKEY) == 0)
        {
            cout << kErrorSubkeyNotSpecified << endl;
            return -1;
        }
        auto pass = GetPassword(vm);
        auto walletDB = OpenDataBase(vm, pass);

        uint32_t subKey = vm[cli::KEY_SUBKEY].as<Positive<uint32_t>>().value;

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

    std::string ReadAssetMeta(const po::variables_map& vm, bool allowOld)
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
        if (allowOld)
        {
            if (meta.isStd_v5_0() || meta.isStd_v6_0())
            {
                return strMeta;
            }
        }
        else
        {
            if (meta.isStd())
            {
                return strMeta;
            }
        }

        throw std::runtime_error(kErrorAssetNonSTDMeta);
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
        info->m_Metadata.get_String(meta);

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
        constexpr Amount limit = static_cast<Amount>(std::numeric_limits<AmountSigned>::max());
        if(!ReadAmount(vm, amountGroth, limit, true))
        {
            return boost::none;
        }

        Amount fee = 0;
        if(!ReadFee(vm, fee, wallet))
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
        if (!ReadFee(vm, fee, wallet))
        {
            return boost::none;
        }

        auto params = CreateTransactionParameters(TxType::AssetReg)
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
        if (!ReadFee(vm, fee, wallet))
        {
            return boost::none;
        }

        auto params = CreateTransactionParameters(TxType::AssetUnreg)
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

    int DoWalletFunc(
        const po::variables_map& vm
        , std::function<int(const po::variables_map&, Wallet::Ptr, IWalletDB::Ptr, boost::optional<TxID>&)> func
#ifdef BEAM_ASSET_SWAP_SUPPORT
        , std::function<int(const po::variables_map&, Wallet::Ptr, IWalletDB::Ptr, DexBoard::Ptr)> func2 =
            [] (const po::variables_map& vm, Wallet::Ptr wallet, IWalletDB::Ptr walletDB, DexBoard::Ptr dex) {return 0;}
#endif  // BEAM_ASSET_SWAP_SUPPORT
        )
    {
        LOG_INFO() << kStartMessage;
        auto walletDB = OpenDataBase(vm);

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

        if (vm[cli::COMMAND].as<string>() == cli::ASSET_INFO || vm.count(cli::ASSET_INFO))
        {
            txCompletedAction = [&onTxCompleteAction, &walletDB](const TxID& txID) {
                auto tx = walletDB->getTx(txID);
                if (tx)
                {
                    const TxDescription& desc = *tx;
                    const auto info = walletDB->findAsset(desc.m_assetId);
                    Amount maxTxAmount = std::numeric_limits<AmountSigned>::max();
                    if (AmountBig::get_Hi(info->m_Value) || AmountBig::get_Lo(info->m_Value) > maxTxAmount)
                    {
                        auto maxTxValue = PrintableAmount(maxTxAmount, true, info->m_ID);
                        cout << "Warning. Total amount of asset would be larger that can be sent in one transaction "
                            << maxTxValue << ". You would be forced to send using several transactions."
                            << endl;
                    }
                }

                onTxCompleteAction(txID);
            };
        }

#ifdef BEAM_ASSET_SWAP_SUPPORT
        AssetsSwapCliHandler assetsSwapHandler;
#endif  // BEAM_ASSET_SWAP_SUPPORT
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
                RegisterAllAssetCreators(*wallet);
#ifdef BEAM_ASSET_SWAP_SUPPORT
                wallet->RegisterTransactionType(
                    TxType::DexSimpleSwap, std::make_shared<DexTransaction::Creator>(walletDB));
#endif  // BEAM_ASSET_SWAP_SUPPORT
            }
#endif  // BEAM_CONFIDENTIAL_ASSETS_SUPPORT
            if (vm.count(cli::REQUEST_BODIES) && vm[cli::REQUEST_BODIES].as<bool>())
            {
                wallet->EnableBodyRequests(true);
            }
            wallet->ResumeAllTransactions();

            auto nnet = CreateNetwork(*wallet, vm);
            if (!nnet)
            {
                return -1;
            }

            auto wnet = make_shared<WalletNetworkViaBbs>(*wallet, nnet, walletDB);
            wallet->AddMessageEndpoint(wnet);
            wallet->SetNodeEndpoint(nnet);

#ifdef BEAM_ASSET_SWAP_SUPPORT
            assetsSwapHandler.init(wallet, walletDB, nnet, *wnet);
#endif  // BEAM_ASSET_SWAP_SUPPORT

            int res = func(vm, wallet, walletDB, currentTxID);
            if (res != 0)
            {
                return res;
            }
#ifdef BEAM_ASSET_SWAP_SUPPORT
            res = func2(vm, wallet, walletDB, assetsSwapHandler.getDex());
            if (res != 0)
            {
                return res;
            }
#endif  // BEAM_ASSET_SWAP_SUPPORT
        }
        io::Reactor::get_Current().run();
        return 0;
    }

    int Send(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                io::Address receiverAddr;
                Asset::ID assetId = Asset::s_BeamID;
                Amount amount = 0;
                Amount fee = 0;
                WalletID receiverWalletID(Zero);

                if (!LoadBaseParamsForTX(vm, *wallet, assetId, amount, fee, receiverWalletID))
                {
                    return -1;
                }

                if (assetId != Asset::s_BeamID)
                {
                    CheckAssetsAllowed(vm);
                }

                auto params = CreateSimpleTransactionParameters();
                LoadReceiverParams(vm, params);

                WalletAddress senderAddress;
                walletDB->createAddress(senderAddress);
                walletDB->saveAddress(senderAddress);

                params.SetParameter(TxParameterID::MyID, senderAddress.m_walletID)
                    .SetParameter(TxParameterID::Amount, amount)
                    // fee for shielded inputs included automatically
                    .SetParameter(TxParameterID::Fee, fee)
                    .SetParameter(TxParameterID::AssetID, assetId)
                    .SetParameter(TxParameterID::PreselectedCoins, GetPreselectedCoinIDs(vm));

                currentTxID = wallet->StartTransaction(params);
                return 0;
            });
    }

    int ShaderInvoke(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](const po::variables_map& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {

			    struct MyManager
				    :public ManagerStdInWallet
			    {
				    bool m_Done = false;
				    bool m_Err = false;
                    bool m_Async = false;

                    using ManagerStdInWallet::ManagerStdInWallet;

                    Wasm::Compiler::DebugInfo m_DbgInfo;

				    void OnDone(const std::exception* pExc) override
				    {
					    m_Done = true;
					    m_Err = !!pExc;

                        if (pExc)
                        {
                            std::cout << "Shader exec error: " << pExc->what() << std::endl;
                            if (m_Debug)
                                DumpCallstack(std::cout, &m_DbgInfo);
                        }
                        else
                            std::cout << "Shader output: " << m_Out.str() << std::endl;

                        if (m_Async)
                            io::Reactor::get_Current().stop();
				    }

                    static void Compile(ByteBuffer& res, const char* sz, Kind kind, Wasm::Compiler::DebugInfo* pDbgInfo = nullptr)
                    {
                        std::FStream fs;
                        fs.Open(sz, true, true);

                        res.resize(static_cast<size_t>(fs.get_Remaining()));
                        if (!res.empty())
                            fs.read(&res.front(), res.size());

                        struct MyCompiler :public bvm2::Processor::Compiler
                        {
                            bool m_HaveMissing;

                            void OnBindingMissing(const Wasm::Compiler::PerImport& x) override
                            {
                                if (!m_HaveMissing)
                                {
                                    m_HaveMissing = true;
                                    std::cout << "Shader uses newer API, some features may not work.\n";
                                }
                                std::cout << "\t Missing " << x << std::endl;
                            }
                        };

                        MyCompiler c;
                        c.m_HaveMissing = false;
                        c.Compile(res, res, kind, pDbgInfo);
                    }
                };

                MyManager man(walletDB, wallet);
                man.m_Debug = vm[cli::SHADER_DEBUG].as<bool>();

                auto sVal = vm[cli::SHADER_BYTECODE_APP].as<string>();
                if (sVal.empty())
                    throw std::runtime_error("shader file not specified");

                MyManager::Compile(man.m_BodyManager, sVal.c_str(), MyManager::Kind::Manager, man.m_Debug ? &man.m_DbgInfo : nullptr);

                sVal = vm[cli::SHADER_BYTECODE_CONTRACT].as<string>();
                if (!sVal.empty())
                    MyManager::Compile(man.m_BodyContract, sVal.c_str(), MyManager::Kind::Contract);

                sVal = vm[cli::SHADER_ARGS].as<string>(); // should be comma-separated list of name=val pairs
                if (!sVal.empty())
                    man.AddArgs(sVal);

                man.set_Privilege(vm[cli::SHADER_PRIVILEGE].as<uint32_t>());

                std::cout << "Executing shader..." << std::endl;

                auto startedEvent = io::AsyncEvent::create(io::Reactor::get_Current(),
                    [&man]()
                    {
                        man.StartRun(man.m_Args.empty() ? 0 : 1); // scheme if no args
                    });

                wallet->DoInSyncedWallet([startedEvent]()
                    {
                        startedEvent->post();
                    });

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

                if (man.m_Err || man.m_InvokeData.m_vec.empty())
                    return 1;

                const auto height  = wallet->get_TipHeight();
                const auto fee     = man.m_InvokeData.get_FullFee(height);
                const auto comment = man.m_InvokeData.get_FullComment();
                const auto spend   = man.m_InvokeData.get_FullSpend();

                std::cout << "Creating new contract invocation tx on behalf of the shader" << std::endl;
                if (man.m_Args["action"] == "create")
                {
                    bvm2::ShaderID sid;
                    bvm2::get_ShaderID(sid, Blob(man.m_BodyContract));
                    for (auto& invokeEntry : man.m_InvokeData.m_vec)
                    {
                        bvm2::ContractID cid;
                        bvm2::get_CidViaSid(cid, sid, Blob(invokeEntry.m_Args));
                        std::cout << "Contract ID: " << cid.str() << std::endl;
                    }
                }
                else if(man.m_Args["action"] == "destroy" && !man.m_Args["cid"].empty())
                {
                    std::cout << "Contract ID: " << man.m_Args["cid"] << std::endl;
                }
                std::cout << "\tComment: " << comment << std::endl;

                for (const auto& info: spend)
                {
                    auto aid = info.first;
                    auto amount = info.second;

                    bool bSpend = (amount > 0);
                    if (!bSpend)
                        amount = -amount;

                    std::cout << '\t' << (bSpend ? "Send" : "Recv") << ' ' << PrintableAmount(static_cast<Amount>(amount), false, aid) << std::endl;
                }

                std::cout << "\tTotal fee: " << PrintableAmount(fee, false, 0) << std::endl;

                ByteBuffer msg(comment.begin(), comment.end());
                currentTxID = wallet->StartTransaction(
                    CreateTransactionParameters(TxType::Contract)
                    .SetParameter(TxParameterID::ContractDataPacked, man.m_InvokeData)
                    .SetParameter(TxParameterID::Message, msg)
                );

                return 0;
            });
    }

    int Listen(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                return 0;
            });
    }

    int Rescan(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
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

        return DoWalletFunc(vm, [&txId](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
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
                    LOG_ERROR() << kErrorCancelTxInInvalidStatus << interpretStatusCliImpl(*tx);
                    return -1;
                }
                LOG_ERROR() << kErrorTxIdUnknown;
                return -1;
            });
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    int InitSwap(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                if (!wallet->IsWalletInSync())
                {
                    return -1;
                }

                currentTxID = InitSwap(vm, walletDB, *wallet);
                if (!currentTxID)
                {
                    return -1;
                }

                return 0;
            });
    }

    int AcceptSwap(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                currentTxID = AcceptSwap(vm, walletDB, *wallet);
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

            cout << "estimate fee rate = " << feeRate << endl;
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

            cout << "avaible: " << balance << endl;
            return 0;
        }

        LOG_ERROR() << "swap_coin should be specified";
        return -1;
    }
#endif // BEAM_ATOMIC_SWAP_SUPPORT

    int IssueAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                currentTxID = IssueConsumeAsset(true, vm, *wallet, walletDB);
                return currentTxID ? 0 : -1;
            });
    }

    int ConsumeAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                currentTxID = IssueConsumeAsset(false, vm, *wallet, walletDB);
                return currentTxID ? 0 : -1;
            });
    }

    int RegisterAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                currentTxID = RegisterAsset(vm, *wallet);
                return currentTxID ? 0 : -1;
            });
    }

    int UnregisterAsset(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                currentTxID = UnregisterAsset(vm, *wallet, walletDB);
                return currentTxID ? 0 : -1;
            });
    }

    int GetAssetInfo(const po::variables_map& vm)
    {
        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                currentTxID = GetAssetInfo(vm, *wallet);
                return currentTxID ? 0: -1;
            });
    }

    int ShowBlockDetails(const po::variables_map& vm)
    {
        auto walletDB = OpenDataBase(vm);

        auto wallet = std::make_shared<Wallet>(walletDB,
            Wallet::TxCompletedAction(),
            Wallet::UpdateCompletedAction());
        auto nnet = CreateNetwork(*wallet, vm);

        if (!nnet)
        {
            return -1;
        }

        if (vm.count(cli::BLOCK_HEIGHT) != 1)
        {
            LOG_ERROR() << "block_height should be specified";
            return -1;
        }

        Height blockHeight = vm[cli::BLOCK_HEIGHT].as<Nonnegative<Height>>().value;

        if (!blockHeight)
        {
            LOG_ERROR() << "Cannot get block header.";
            return -1;
        }

        proto::FlyClient::RequestEnumHdrs::Ptr pRequest(new proto::FlyClient::RequestEnumHdrs);
        proto::FlyClient::RequestEnumHdrs& request = *pRequest;

        request.m_Msg.m_Height = blockHeight;

        struct MyHandler : public proto::FlyClient::Request::IHandler
        {
            proto::FlyClient::Request* m_Request = nullptr;

            ~MyHandler() {
                m_Request->m_pTrg = nullptr;
            }

            virtual void OnComplete(proto::FlyClient::Request&) override
            {
                m_Request->m_pTrg = nullptr;
                io::Reactor::get_Current().stop();
            }

        } myHandler;

        myHandler.m_Request = &request;
        nnet->PostRequest(request, myHandler);

        io::Reactor::get_Current().run();

        if (request.m_pTrg || request.m_vStates.size() != 1)
        {
            LOG_ERROR() << "Cannot get block header.";
            return -1;
        }

        Block::SystemState::Full state = request.m_vStates.front();
        Merkle::Hash blockHash;
        state.get_Hash(blockHash);

        std::string rulesHash = Rules::get().pForks[_countof(Rules::get().pForks) - 1].m_Hash.str();

        for (size_t i = 1; i < _countof(Rules::get().pForks); i++)
        {
            if (state.m_Height < Rules::get().pForks[i].m_Height)
            {
                rulesHash = Rules::get().pForks[i - 1].m_Hash.str();
                break;
            }
        }

        std::cout << "Block Details:" << "\n"
            << "Height: " << state.m_Height << "\n"
            << "Hash: " << blockHash.str() << "\n"
            << "Previous state hash: " << state.m_Prev.str() << "\n"
            << "ChainWork: " << state.m_ChainWork.str() << "\n"
            << "Kernels: " << state.m_Kernels.str() << "\n"
            << "Definition: " << state.m_Definition.str() << "\n"
            << "Timestamp: " << state.m_TimeStamp << "\n"
            << "POW: " << beam::to_hex(&state.m_PoW, sizeof(state.m_PoW)) << "\n"
            << "Difficulty: " << state.m_PoW.m_Difficulty.ToFloat() << "\n"
            << "Packed difficulty: " << state.m_PoW.m_Difficulty.m_Packed << "\n"
            << "Rules hash: " << rulesHash << "\n";

        return 0;
    }

    int ImportRecovery(const po::variables_map& vm)
    {
        if (vm[cli::IMPORT_EXPORT_PATH].defaulted())
        {
            LOG_ERROR() << kErrorFileLocationParamReqired;
            return -1;
        }

        struct MyProgress : IWalletDB::IRecoveryProgress
        {
            virtual bool OnProgress(uint64_t done, uint64_t total)
            {
                size_t percent = done * 100 / total;
                std::cout << "\rImporting recovery data: " << percent << "% (" << done << "/" << total << ")";
                std::cout.flush();
                return true; 
            }
        };
        auto path = vm[cli::IMPORT_EXPORT_PATH].as<string>();
        return DoWalletFunc(vm, [&path](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
            {
                MyProgress progress;
                walletDB->ImportRecovery(path, *wallet, progress);
                return 1;
            });
    }

    int AssetsSwapList(const po::variables_map& vm)
    {
        CheckAssetsAllowed(vm);

        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
        {
            return 0;
        },
        [](auto&& vm, auto&& wallet, auto&& walletDB, DexBoard::Ptr dex)
        {
            auto orders = dex->getDexOrders();

            const array<uint8_t, 10> columnWidths{ {32, 18, 13, 10, 18, 16, 10, 13, 15, 6} };
            cout << boost::format(kAssetsSwapTableHead)
                % boost::io::group(left, setw(columnWidths[0]), kAssetsSwapID)
                % boost::io::group(right, setw(columnWidths[1]), kAssetsSwapSendAmount)
                % boost::io::group(right, setw(columnWidths[2]), kAssetsSwapSendAssetID)
                % boost::io::group(left, setw(columnWidths[3]), kAssetsSwapSendAssetName)
                % boost::io::group(right, setw(columnWidths[4]), kAssetsSwapReceiveAmount)
                % boost::io::group(right, setw(columnWidths[5]), kAssetsSwapReceiveAssetID)
                % boost::io::group(left, setw(columnWidths[6]), kAssetsSwapReceiveAssetName)
                % boost::io::group(right, setw(columnWidths[7]), kAssetsSwapCreation)
                % boost::io::group(right, setw(columnWidths[8]), kAssetsSwapExpiration)
                % boost::io::group(left, setw(columnWidths[9]), kAssetsSwapIsMine)
                << std::endl;

            for (auto order : orders)
            {
                cout << boost::format(kAssetsSwapTableFormat)
                    % boost::io::group(left,setw(columnWidths[0]), order.getID().to_string())
                    % boost::io::group(right,setw(columnWidths[1]), order.getSendAmount())
                    % boost::io::group(right,setw(columnWidths[2]), order.getSendAssetId())
                    % boost::io::group(left, setw(columnWidths[3]), order.getSendAssetSName())
                    % boost::io::group(right, setw(columnWidths[4]), order.getReceiveAmount())
                    % boost::io::group(right, setw(columnWidths[5]), order.getReceiveAssetId())
                    % boost::io::group(left,setw(columnWidths[6]), order.getReceiveAssetSName())
                    % boost::io::group(right,setw(columnWidths[7]), order.getCreation())
                    % boost::io::group(right,setw(columnWidths[8]), order.getExpiration())
                    % boost::io::group(left, setw(columnWidths[9]), order.isMine() ? "true" : "false")
                  << std::endl;
            }
            return 1;
        });
    }

    int AssetSwapCreate(const po::variables_map& vm)
    {
        CheckAssetsAllowed(vm);

        auto sendAssetId = Asset::s_InvalidID;
        if (auto it = vm.find(cli::ASSETS_SWAP_SEND_ASSET_ID); it != vm.end())
        {
            sendAssetId = vm[cli::ASSETS_SWAP_SEND_ASSET_ID].as<Nonnegative<uint32_t>>().value;
        }
        else
        {
            cout << "You must provide send asset id";
            return -1;
        }

        auto receiveAssetId = Asset::s_InvalidID;
        if (auto it = vm.find(cli::ASSETS_SWAP_RECEIVE_ASSET_ID); it != vm.end())
        {
            receiveAssetId = vm[cli::ASSETS_SWAP_RECEIVE_ASSET_ID].as<Nonnegative<uint32_t>>().value;
        }
        else
        {
            cout << "You must provide receive asset id";
            return -1;
        }

        if (receiveAssetId == sendAssetId)
        {
            cout << "Send and receive assets ID can't be identical.";
            return -1;
        }

        if (auto it = vm.find(cli::ASSETS_SWAP_SEND_AMOUNT); it == vm.end())
        {
            cout << "You must provide send amount";
            return -1;
        }

        auto sendAmount = vm[cli::ASSETS_SWAP_SEND_AMOUNT].as<Positive<double>>().value;
        sendAmount *= Rules::Coin;
        Amount sendAmountGroth = static_cast<Amount>(std::round(sendAmount));

        if (auto it = vm.find(cli::ASSETS_SWAP_RECEIVE_AMOUNT); it == vm.end())
        {
            cout << "You must provide receive amount";
            return -1;
        }

        auto receiveAmount = vm[cli::ASSETS_SWAP_RECEIVE_AMOUNT].as<Positive<double>>().value;
        receiveAmount *= Rules::Coin;
        Amount receiveAmountGroth = static_cast<ECC::Amount>(std::round(receiveAmount));

        unsigned expirationMinutes = vm[cli::ASSETS_SWAP_EXPIRATION].as<uint32_t>();

        if (expirationMinutes < 30 || expirationMinutes > 720)
        {
            cout << "minutes_before_expire must be > 30 and < 720";
            return -1;
        }

        auto comment = vm[cli::ASSETS_SWAP_COMMENT].as<string>();

        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
        {
            return 0;
        },
        [sendAssetId, receiveAssetId, sendAmountGroth,
         receiveAmountGroth, expirationMinutes, comment](auto&& vm, auto&& wallet, auto&& walletDB, DexBoard::Ptr dex)
        {
            std::string sendAssetUnitName = "BEAM";
            if (sendAssetId)
            {
                auto sendAssetInfo = walletDB->findAsset(sendAssetId);
                if (!sendAssetInfo)
                {
                    cout << "ERROR: Unknown asset id - " << cli::ASSETS_SWAP_SEND_ASSET_ID;
                }
                const auto &sendAssetMeta = WalletAssetMeta(*sendAssetInfo);
                sendAssetUnitName = sendAssetMeta.GetUnitName();
            }

            std::string receiveAssetUnitName = "BEAM";
            if (receiveAssetId)
            {
                auto receiveAssetInfo = walletDB->findAsset(receiveAssetId);
                if (!receiveAssetInfo)
                {
                    cout << "ERROR: Unknown asset id - " << cli::ASSETS_SWAP_RECEIVE_ASSET_ID;
                }
                const auto &receiveAssetMeta = WalletAssetMeta(*receiveAssetInfo);
                receiveAssetUnitName = receiveAssetMeta.GetUnitName();
            }

            WalletAddress receiverAddress;
            walletDB->createAddress(receiverAddress);
            receiverAddress.m_label = comment;
            receiverAddress.m_duration = beam::wallet::WalletAddress::AddressExpirationAuto;
            walletDB->saveAddress(receiverAddress);

            DexOrder orderObj(
                DexOrderID::generate(),
                receiverAddress.m_walletID,
                receiverAddress.m_OwnID,
                sendAssetId,
                sendAmountGroth,
                sendAssetUnitName,
                receiveAssetId,
                receiveAmountGroth,
                receiveAssetUnitName,
                expirationMinutes * 60
            );

            dex->publishOrder(orderObj);

            cout << "Order created with ID: " << orderObj.getID() << std::endl
                 << "Waiting for order appruv" << std::endl;;

            return 0;
        });

        return 1;
    }

    int AssetSwapCancel(const po::variables_map& vm)
    {
        CheckAssetsAllowed(vm);

        std::string offerIdStr;
        DexOrderID offerId;
        if (auto it = vm.find(cli::ASSETS_SWAP_OFFER_ID); it != vm.end())
        {
            offerIdStr = vm[cli::ASSETS_SWAP_OFFER_ID].as<string>();
            if (offerIdStr.empty())
            {
                cout << cli::ASSETS_SWAP_OFFER_ID << " is empty.";
                return -1;
            }

            if (!offerId.FromHex(offerIdStr))
            {
                cout << cli::ASSETS_SWAP_OFFER_ID << " is malformed";
                return -1;
            }
        }
        else
        {
            cout << cli::ASSETS_SWAP_OFFER_ID << " is not provided";
            return -1;
        }

        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
        {
            return 0;
        },
        [offerId, offerIdStr](auto&& vm, auto&& wallet, auto&& walletDB, DexBoard::Ptr dex)
        {
            auto order = dex->getDexOrder(offerId);
            if (!order)
            {
                cout << "offer with " << cli::ASSETS_SWAP_OFFER_ID << ":" << offerIdStr << "is not found";
                return -1;
            }

            if (!order->isMine())
            {
                cout << "offer with " << cli::ASSETS_SWAP_OFFER_ID << ":" << offerIdStr << "is not your offer";
                return -1;
            }

            order->cancel();
            dex->publishOrder(*order);

            cout << "offer with " << cli::ASSETS_SWAP_OFFER_ID << ":" << offerIdStr << " cancelled" << std::endl;
            
            return 0;
        });

        return 1;
    }

    int AssetSwapAccept(const po::variables_map& vm)
    {
        CheckAssetsAllowed(vm);

        std::string offerIdStr;
        DexOrderID offerId;
        if (auto it = vm.find(cli::ASSETS_SWAP_OFFER_ID); it != vm.end())
        {
            offerIdStr = vm[cli::ASSETS_SWAP_OFFER_ID].as<string>();
            if (offerIdStr.empty())
            {
                cout << cli::ASSETS_SWAP_OFFER_ID << " is empty.";
                return -1;
            }

            if (!offerId.FromHex(offerIdStr))
            {
                cout << cli::ASSETS_SWAP_OFFER_ID << " is malformed";
                return -1;
            }
        }
        else
        {
            cout << cli::ASSETS_SWAP_OFFER_ID << " is not provided";
            return -1;
        }

        auto comment = vm[cli::ASSETS_SWAP_COMMENT].as<string>();

        return DoWalletFunc(vm, [](auto&& vm, auto&& wallet, auto&& walletDB, auto& currentTxID)
        {
            return 0;
        },
        [offerId, offerIdStr, comment](auto&& vm, auto&& wallet, auto&& walletDB, DexBoard::Ptr dex)
        {
            auto order = dex->getDexOrder(offerId);
            if (!order)
            {
                cout << "offer with " << cli::ASSETS_SWAP_OFFER_ID << ":" << offerIdStr << "is not found";
                return -1;
            }

            if (order->isMine())
            {
                cout << "offer with " << cli::ASSETS_SWAP_OFFER_ID << ":" << offerIdStr << "is your offer";
                return -1;
            }

            Asset::ID sendAsset = order->getSendAssetId();
            if (sendAsset)
            {
                auto sendAssetInfo = walletDB->findAsset(sendAsset);
                if (!sendAssetInfo)
                {
                    cout << "Not enough funds to send for " << cli::ASSETS_SWAP_OFFER_ID << ":" << offerIdStr;
                    return -1;
                }
            }

            Asset::ID receiveAsset = order->getReceiveAssetId();
            if (receiveAsset)
            {
                auto receiveAssetInfo = walletDB->findAsset(receiveAsset);
                if (!receiveAssetInfo)
                {
                    cout << "Unknown receive asset id in order " << cli::ASSETS_SWAP_OFFER_ID << ":" << offerIdStr;
                    return -1;
                }
            }

            Amount fee = 100000;
            CoinsSelectionInfo csi;
            csi.m_requestedSum = order->getSendAmount();
            csi.m_assetID = sendAsset;
            csi.m_explicitFee = fee;
            csi.Calculate(walletDB->getCurrentHeight(), walletDB, false);

            if (!csi.m_isEnought)
            {
                cout << "Not enough funds to send for " << cli::ASSETS_SWAP_OFFER_ID << ":" << offerIdStr;
                return -1;
            }

            WalletAddress myAddress;
            walletDB->createAddress(myAddress);
            myAddress.m_label = comment;
            myAddress.m_duration = beam::wallet::WalletAddress::AddressExpirationAuto;
            walletDB->saveAddress(myAddress);

            auto params = beam::wallet::CreateDexTransactionParams(
                            offerId,
                            order->getSBBSID(),
                            myAddress.m_walletID,
                            sendAsset,
                            order->getSendAmount(),
                            receiveAsset,
                            order->getReceiveAmount(),
                            fee
                            );

            
            auto txId = wallet->StartTransaction(params);
            
            cout << "Asset swap for offer id: " << offerIdStr << " started" << std::endl
                 << "TxId: " << txId << std::endl;
            return 0;
        });

        return 1;
    }
}  // namespace

io::Reactor::Ptr reactor;

static const unsigned LOG_ROTATION_PERIOD_SEC = 3*60*60; // 3 hours

int main(int argc, char* argv[])
{
    beam::Crash::InstallHandler(NULL);
    const Command commands[] =
    {
        {cli::INIT,               InitWallet,                       "initialize new wallet database with a new seed phrase"},
        {cli::RESTORE,            RestoreWallet,                    "restore wallet database from a seed phrase provided by the user"},
        {cli::RESTORE_USB,        RestoreWalletUsb,                 "restore wallet database from an attached HW wallet"},
        {cli::USB_ENUM,           EnumUsb,                          "Enumerate attached HW wallets"},
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
        {cli::BLOCK_DETAILS,        ShowBlockDetails,               "print information about specified block"},
        {cli::IMPORT_RECOVERY,      ImportRecovery,                 "import block data from recovery file"},
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        {cli::SWAP_INIT,            InitSwap,                       "initialize atomic swap"},
        {cli::SWAP_ACCEPT,          AcceptSwap,                     "accept atomic swap offer"},
        {cli::SET_SWAP_SETTINGS,    SetSwapSettings,                "set generic atomic swap settings"},
        {cli::SHOW_SWAP_SETTINGS,   ShowSwapSettings,               "print BTC/LTC/QTUM/DASH/DOGE/ETH-specific swap settings"},
        {cli::ESTIMATE_SWAP_FEERATE, EstimateSwapFeerate,           "estimate BTC/LTC/QTUM/DASH/DOGE/ETH-specific fee rate"},
        {cli::GET_BALANCE,          GetBalance,                     "get BTC/LTC/QTUM/DASH/DOGE/ETH balance"},
#endif // BEAM_ATOMIC_SWAP_SUPPORT
        {cli::GET_ADDRESS,            GetAddress,                   "generate transaction address for a specific receiver (identifiable by SBBS address or wallet's signature)"},
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
#ifdef BEAM_ASSET_SWAP_SUPPORT
        {cli::ASSETS_SWAP_LIST,     AssetsSwapList,                 "view available assets swap list"},
        {cli::ASSETS_SWAP_CREATE,   AssetSwapCreate,                "create asset swap"},
        {cli::ASSETS_SWAP_CANCEL,   AssetSwapCancel,                "cancel asset swap"},
        {cli::ASSETS_SWAP_ACCEPT,   AssetSwapAccept,                "accept asset swap"},
#endif  // BEAM_ASSET_SWAP_SUPPORT
    };

    try
    {
        auto [options, visibleOptions] = createOptionsDescription(GENERAL_OPTIONS | WALLET_OPTIONS, kDefaultConfigFile);

        po::variables_map vm;
        try
        {
            vm = getOptions(argc, argv, options, true);
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

        int logLevel = getLogLevel(cli::LOG_LEVEL, vm, LOG_LEVEL_INFO);
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

            // always enabled since beamX
            wallet::g_AssetsEnabled = true;

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
