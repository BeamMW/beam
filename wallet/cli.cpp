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
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "core/treasury.h"
#include "unittests/util.h"
#include "mnemonic/mnemonic.h"
#include "utility/string_helpers.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include "utility/cli/options.h"
#include "utility/log_rotation.h"
#include "utility/helpers.h"
#include <iomanip>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <iterator>
#include <future>
#include "version.h"

using namespace std;
using namespace beam;
using namespace ECC;

namespace beam
{
    std::ostream& operator<<(std::ostream& os, Coin::Status s)
    {
        stringstream ss;
        ss << "[";
        switch (s)
        {
        case Coin::Available: ss << "Available"; break;
        case Coin::Unavailable: ss << "Unavailable"; break;
        case Coin::Spent: ss << "Spent"; break;
        case Coin::Maturing: ss << "Maturing"; break;
        case Coin::Outgoing: ss << "In progress(outgoing)"; break;
        case Coin::Incoming: ss << "In progress(incoming/change)"; break;
        default:
            assert(false && "Unknown coin status");
        }
        ss << "]";
        string str = ss.str();
        os << str;
        assert(str.length() <= 30);
        return os;
    }

    const char* getTxStatus(const TxDescription& tx)
    {
        static const char* Pending = "pending";
        static const char* WaitingForSender = "waiting for sender";
        static const char* WaitingForReceiver = "waiting for receiver";
        static const char* Sending = "sending";
        static const char* Receiving = "receiving";
        static const char* Cancelled = "cancelled";
        static const char* Sent = "sent";
        static const char* Received = "received";
        static const char* Failed = "failed";
        static const char* Completed = "completed";
        static const char* Expired = "expired";

        switch (tx.m_status)
        {
        case TxStatus::Pending: return Pending;
        case TxStatus::InProgress: return tx.m_sender ? WaitingForReceiver : WaitingForSender;
        case TxStatus::Registering: return tx.m_sender ? Sending : Receiving;
        case TxStatus::Cancelled: return Cancelled;
        case TxStatus::Completed:
        {
            if (tx.m_selfTx)
            {
                return Completed;
            }
            return tx.m_sender ? Sent : Received;
        }
        case TxStatus::Failed: return TxFailureReason::TransactionExpired == tx.m_failureReason ? Expired : Failed;
        default:
            assert(false && "Unknown status");
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
            throw std::runtime_error("invalid WID");

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
            cout << "ID: " << szID << std::endl;
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
                throw std::runtime_error("bad m/n");

            assert(n);
            if (pars.m_Bursts % n)
                throw std::runtime_error("bad n (roundoff)");

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
                        throw std::runtime_error("Nothing remains");

                    cout << "Maturity=" << c.m_Incubation << ", Consumed = " << vL << " / " << c.m_Value << std::endl;
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
                throw std::runtime_error("plan not found");

            Treasury::Entry& e = it->second;
            e.m_pResponse.reset(new Treasury::Response);
            FLoad(*e.m_pResponse, sID + szResponse);

            if (!e.m_pResponse->IsValid(e.m_Request))
                throw std::runtime_error("invalid response");

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

            cout << "Treasury data hash: " << szHash << std::endl;

        }
        break;

        case 5:
        {
            // recover and print
            Treasury::Data data;
            FLoad(data, szData);

            std::vector<Treasury::Data::Coin> vCoins;
            data.Recover(kdf, vCoins);

            cout << "Recovered coins: " << vCoins.size() << std::endl;

            for (size_t i = 0; i < vCoins.size(); i++)
            {
                const Treasury::Data::Coin& coin = vCoins[i];
                cout << "\t" << coin.m_Kidv << ", Height=" << coin.m_Incubation << std::endl;

            }
        }
        break;

        case 6:
        {
            // bursts
            Treasury::Data data;
            FLoad(data, szData);

            auto vBursts = data.get_Bursts();

            cout << "Total bursts: " << vBursts.size() << std::endl;

            for (size_t i = 0; i < vBursts.size(); i++)
            {
                const Treasury::Data::Burst& b = vBursts[i];
                cout << "\t" << "Height=" << b.m_Height << ", Value=" << b.m_Value << std::endl;
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
        string newTime = vm[cli::EXPIRATION_TIME].as<string>();
        WalletID walletID(Zero);
        bool allAddresses = address == "*";

        if (!allAddresses)
        {
            walletID.FromHex(address);
        }
        uint64_t newDuration_s = 0;
        if (newTime == "24h")
        {
            newDuration_s = 24 * 3600; //seconds
        }
        else if (newTime == "never")
        {
            newDuration_s = 0;
        }
        else
        {
            LOG_ERROR() << "Invalid address expiration time \"" << newTime << "\".";
            return -1;
        }

        if (wallet::changeAddressExpiration(*walletDB, walletID, newDuration_s))
        {
            if (allAddresses)
            {
                LOG_INFO() << "Expiration for all addresses  was changed to \"" << newTime << "\".";
            }
            else
            {
                LOG_INFO() << "Expiration for address " << to_string(walletID) << " was changed to \"" << newTime << "\".";
            }
            return 0;
        }
        return -1;
    }

    WalletAddress newAddress(const IWalletDB::Ptr& walletDB, const std::string& comment, bool isNever = false)
    {
        WalletAddress address = wallet::createAddress(*walletDB);

        if (isNever)
        {
            address.m_duration = 0;
        }

        address.m_label = comment;
        walletDB->saveAddress(address);

        LOG_INFO() << "New address generated:\n\n" << std::to_string(address.m_walletID) << "\n";
        if (!comment.empty()) {
            LOG_INFO() << "comment = " << comment;
        }
        return address;
    }

    WordList GeneratePhrase()
    {
        auto phrase = createMnemonic(getEntropy(), language::en);
        assert(phrase.size() == 12);
        cout << "======\nGenerated seed phrase: \n\n\t";
        for (const auto& word : phrase)
        {
            cout << word << ';';
        }
        cout << "\n\n\tIMPORTANT\n\n\tYour seed phrase is the access key to all the cryptocurrencies in your wallet.\n\tPrint or write down the phrase to keep it in a safe or in a locked vault.\n\tWithout the phrase you will not be able to recover your money.\n======" << endl;
        return phrase;
    }

    bool ReadWalletSeed(NoLeak<uintBig>& walletSeed, const po::variables_map& vm, bool generateNew)
    {
        SecString seed;
        WordList phrase;
        if (generateNew)
        {
            LOG_INFO() << "Generating seed phrase...";
            phrase = GeneratePhrase();
        }
        else if (vm.count(cli::SEED_PHRASE))
        {
            auto tempPhrase = vm[cli::SEED_PHRASE].as<string>();
            phrase = string_helpers::split(tempPhrase, ';');
            assert(phrase.size() == 12);
            if (!isValidMnemonic(phrase, language::en))
            {
                LOG_ERROR() << "Invalid seed phrases provided: " << tempPhrase;
                return false;
            }
        }
        else
        {
            LOG_ERROR() << "Seed phrase has not been provided.";
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
        cout << "Addresses\n\n"
            << "  " << std::left
            << setw(columnWidths[0]) << "comment" << "|"
            << setw(columnWidths[1]) << "address" << "|"
            << setw(columnWidths[2]) << "active" << "|"
            << setw(columnWidths[3]) << "expiration date" << "|"
            << setw(columnWidths[4]) << "created" << endl;

        for (const auto& address : addresses)
        {
            auto comment = address.m_label;

            if (comment.length() > columnWidths[0])
            {
                comment = comment.substr(0, columnWidths[0] - 3) + "...";
            }

            auto expirationDateText = (address.m_duration == 0) ? "never" : format_timestamp("%Y.%m.%d %H:%M:%S", address.getExpirationTime() * 1000, false);

            cout << "  " << std::left << std::boolalpha
                << setw(columnWidths[0]) << comment << " "
                << setw(columnWidths[1]) << std::to_string(address.m_walletID) << " "
                << setw(columnWidths[2]) << !address.isExpired() << " "
                << setw(columnWidths[3]) << expirationDateText << " "
                << setw(columnWidths[4]) << format_timestamp("%Y.%m.%d %H:%M:%S", address.getCreateTime() * 1000, false) << "\n";
        }

        return 0;
    }

    int ShowWalletInfo(const IWalletDB::Ptr& walletDB, const po::variables_map& vm)
    {
        Block::SystemState::ID stateID = {};
        walletDB->getSystemStateID(stateID);

		wallet::Totals totals(*walletDB);

        cout << "____Wallet summary____\n\n"
            << "Current height............" << stateID.m_Height << '\n'
            << "Current state ID.........." << stateID.m_Hash << "\n\n"
            << "Available................." << PrintableAmount(totals.Avail) << '\n'
            << "Maturing.................." << PrintableAmount(totals.Maturing) << '\n'
            << "In progress..............." << PrintableAmount(totals.Incoming) << '\n'
            << "Unavailable..............." << PrintableAmount(totals.Unavail) << '\n'
            << "Available coinbase ......." << PrintableAmount(totals.AvailCoinbase) << '\n'
            << "Total coinbase............" << PrintableAmount(totals.Coinbase) << '\n'
            << "Avaliable fee............." << PrintableAmount(totals.AvailFee) << '\n'
            << "Total fee................." << PrintableAmount(totals.Fee) << '\n'
            << "Total unspent............." << PrintableAmount(totals.Unspent) << "\n\n";

        if (vm.count(cli::TX_HISTORY))
        {
            auto txHistory = walletDB->getTxHistory();
            if (txHistory.empty())
            {
                cout << "No transactions\n";
                return 0;
            }

            const array<uint8_t, 5> columnWidths{ { 20, 26, 21, 33, 65} };

            cout << "TRANSACTIONS\n\n  |"
                << left << setw(columnWidths[0]) << " datetime" << " |"
                << right << setw(columnWidths[1]) << " amount, BEAM" << " |"
                << left << setw(columnWidths[2]) << " status" << " |"
                << setw(columnWidths[3]) << " ID" << " |" 
                << setw(columnWidths[4]) << " kernel ID" << " |" << endl;

            for (auto& tx : txHistory)
            {
                cout << "   "
                    << " " << left << setw(columnWidths[0]) << format_timestamp("%Y.%m.%d %H:%M:%S", tx.m_createTime * 1000, false)
                    << " " << right << setw(columnWidths[1]) << PrintableAmount(tx.m_amount, true) << " "
                    << " " << left << setw(columnWidths[2]+1) << getTxStatus(tx) 
                    << " " << setw(columnWidths[3]+1) << to_hex(tx.m_txId.data(), tx.m_txId.size())
                    << " " << setw(columnWidths[4]+1) << to_string(tx.m_kernelID) << '\n';
            }
            return 0;
        }
        const array<uint8_t, 6> columnWidths{ { 49, 14, 14, 18, 30, 8} };
        cout << "  |"
            << left << setw(columnWidths[0]) << " ID" << " |"
            << right << setw(columnWidths[1]) << " beam" << " |"
            << setw(columnWidths[2]) << " groth" << " |"
            << left << setw(columnWidths[3]) << " maturity" << " |"
            << setw(columnWidths[4]) << " status" << " |"
            << setw(columnWidths[5]) << " type" << endl;

        
        walletDB->visit([&columnWidths](const Coin& c)->bool
        {
            cout << "   "
                << " " << left << setw(columnWidths[0]) << c.toStringID()
                << " " << right << setw(columnWidths[1]) << c.m_ID.m_Value / Rules::Coin << " "
                << " " << right << setw(columnWidths[2]) << c.m_ID.m_Value % Rules::Coin << "  "
                << " " << left << setw(columnWidths[3]+1) << (c.IsMaturityValid() ? std::to_string(static_cast<int64_t>(c.m_maturity)) : "-")
                << " " << setw(columnWidths[4]+1) << c.m_status
                << " " << setw(columnWidths[5]+1) << c.m_ID.m_Type << endl;
            return true;
        });
        return 0;
    }

    int ExportPaymentProof(const IWalletDB::Ptr& walletDB, const po::variables_map& vm)
    {
        auto txIdVec = from_hex(vm[cli::TX_ID].as<string>());
        TxID txId;
        if (txIdVec.size() >= 16)
            std::copy_n(txIdVec.begin(), 16, txId.begin());

        auto res = wallet::ExportPaymentProof(*walletDB, txId);
        if (!res.empty())
        {
            std::string sTxt;
            sTxt.resize(res.size() * 2);

            beam::to_hex(&sTxt.front(), res.data(), res.size());
            LOG_INFO() << "Exported form: " << sTxt;
        }

        return 0;
    }

    int VerifyPaymentProof(const po::variables_map& vm)
    {
        ByteBuffer buf = from_hex(vm[cli::PAYMENT_PROOF_DATA].as<string>());

        if (!wallet::VerifyPaymentProof(buf))
            throw std::runtime_error("Payment proof is invalid");

        return 0;
    }

    int ExportMinerKey(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, const beam::SecString& pass)
    {
        uint32_t subKey = vm[cli::KEY_SUBKEY].as<uint32_t>();
        if (subKey < 1)
        {
            cout << "Please, specify Subkey number --subkey=N (N > 0)" << endl;
            return -1;
        }
        Key::IKdf::Ptr pKey = walletDB->get_ChildKdf(subKey);
        const ECC::HKdf& kdf = static_cast<ECC::HKdf&>(*pKey);

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(subKey);

        ks.Export(kdf);
        cout << "Secret Subkey " << subKey << ": " << ks.m_sRes << std::endl;

        return 0;
    }

    int ExportOwnerKey(const IWalletDB::Ptr& walletDB, const beam::SecString& pass)
    {
        Key::IKdf::Ptr pKey = walletDB->get_ChildKdf(0);
        const ECC::HKdf& kdf = static_cast<ECC::HKdf&>(*pKey);

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(0);

        ECC::HKdfPub pkdf;
        pkdf.GenerateFrom(kdf);

        ks.Export(pkdf);
        cout << "Owner Viewer key: " << ks.m_sRes << std::endl;

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
        FStream f;
        if (f.Open(path.c_str(), false))
        {
            return f.write(data.data(), data.size()) == data.size();
        }
        LOG_ERROR() << "Failed to save exported data";
        return false;
    }

    int ExportAddresses(const po::variables_map& vm, const IWalletDB::Ptr& walletDB)
    {
        auto s = wallet::ExportAddressesToJson(*walletDB);
        return SaveExportedData(ByteBuffer(s.begin(), s.end()), vm[cli::IMPORT_EXPORT_PATH].as<string>()) ? 0 : -1;
    }

    int ImportAddresses(const po::variables_map& vm, const IWalletDB::Ptr& walletDB)
    {
        ByteBuffer buffer;
        if (!LoadDataToImport(vm[cli::IMPORT_EXPORT_PATH].as<string>(), buffer))
        {
            return -1;
        }
        const char* p = (char*)(&buffer[0]);
        return wallet::ImportAddressesFromJson(*walletDB, p, buffer.size()) ? 0 : -1;
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
            vm = getOptions(argc, argv, "beam-wallet.cfg", options, true);
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
                        LOG_ERROR() << "command parameter not specified.";
                        printHelp(visibleOptions);
                        return 0;
                    }

                    auto command = vm[cli::COMMAND].as<string>();

                    if (command != cli::INIT
                        && command != cli::RESTORE
                        && command != cli::SEND
                        && command != cli::RECEIVE
                        && command != cli::LISTEN
                        && command != cli::TREASURY
                        && command != cli::INFO
                        && command != cli::EXPORT_MINER_KEY
                        && command != cli::EXPORT_OWNER_KEY
                        && command != cli::NEW_ADDRESS
                        && command != cli::CANCEL_TX
                        && command != cli::CHANGE_ADDRESS_EXPIRATION
                        && command != cli::PAYMENT_PROOF_EXPORT
                        && command != cli::PAYMENT_PROOF_VERIFY
                        && command != cli::GENERATE_PHRASE
                        && command != cli::WALLET_ADDRESS_LIST
                        && command != cli::WALLET_RESCAN
                        && command != cli::IMPORT_ADDRESSES
                        && command != cli::EXPORT_ADDRESSES)
                    {
                        LOG_ERROR() << "unknown command: \'" << command << "\'";
                        return -1;
                    }

                    if (command == cli::GENERATE_PHRASE)
                    {
                        GeneratePhrase();
                        return 0;
                    }

                    LOG_INFO() << "Beam Wallet " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
                    LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

                    assert(vm.count(cli::WALLET_STORAGE) > 0);
                    auto walletPath = vm[cli::WALLET_STORAGE].as<string>();

                    if (!WalletDB::isInitialized(walletPath) && (command != cli::INIT && command != cli::RESTORE))
                    {
                        LOG_ERROR() << "Please initialize your wallet first... \nExample: beam-wallet --command=init";
                        return -1;
                    }
                    else if (WalletDB::isInitialized(walletPath) && (command == cli::INIT || command == cli::RESTORE))
                    {
                        LOG_ERROR() << "Your wallet is already initialized.";
                        return -1;
                    }

                    LOG_INFO() << "starting a wallet...";

                    SecString pass;
                    if (!beam::read_wallet_pass(pass, vm))
                    {
                        LOG_ERROR() << "Please, provide password for the wallet.";
                        return -1;
                    }

                    if ((command == cli::INIT || command == cli::RESTORE) && vm.count(cli::PASS) == 0)
                    {
                        if (!beam::confirm_wallet_pass(pass))
                        {
                            LOG_ERROR() << "Passwords do not match";
                            return -1;
                        }
                    }

                    bool coldWallet = vm.count(cli::COLD_WALLET) > 0;

                    if (command == cli::INIT || command == cli::RESTORE)
                    {
                        NoLeak<uintBig> walletSeed;
                        walletSeed.V = Zero;
                        if (!ReadWalletSeed(walletSeed, vm, command == cli::INIT))
                        {
                            LOG_ERROR() << "Please, provide seed phrase for the wallet.";
                            return -1;
                        }
                        auto walletDB = WalletDB::init(walletPath, pass, walletSeed, reactor, coldWallet);
                        if (walletDB)
                        {
                            LOG_INFO() << "wallet successfully created...";

                            // generate default address
                            newAddress(walletDB, "default");

                            return 0;
                        }
                        else
                        {
                            LOG_ERROR() << "something went wrong, wallet not created...";
                            return -1;
                        }
                    }

                    auto walletDB = WalletDB::open(walletPath, pass, reactor);
                    if (!walletDB)
                    {
                        LOG_ERROR() << "Please check your password. If password is lost, restore wallet.db from latest backup or delete it and restore from seed phrase.";
                        return -1;
                    }

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

                    if (command == cli::EXPORT_ADDRESSES)
                    {
                        return ExportAddresses(vm, walletDB);
                    }

                    if (command == cli::IMPORT_ADDRESSES)
                    {
                        return ImportAddresses(vm, walletDB);
                    }

                    {
                        const auto& var = vm[cli::PAYMENT_PROOF_REQUIRED];
                        if (!var.empty())
                        {
                            bool b = var.as<bool>();
                            uint8_t n = b ? 1 : 0;
                            wallet::setVar(*walletDB, wallet::g_szPaymentProofRequired, n);

                            cout << "Parameter set: Payment proof required: " << static_cast<uint32_t>(n) << std::endl;
                            return 0;
                        }
                    }

                    if (command == cli::NEW_ADDRESS)
                    {
                        auto comment = vm[cli::NEW_ADDRESS_COMMENT].as<string>();
                        newAddress(walletDB, comment, vm[cli::EXPIRATION_TIME].as<string>() == "never");

                        if (!vm.count(cli::LISTEN)) 
                        {
                            return 0;
                        }
                    }

                    LOG_INFO() << "wallet sucessfully opened...";

                    if (command == cli::TREASURY)
                    {
                        return HandleTreasury(vm, *walletDB->get_MasterKdf());
                    }

                    if (command == cli::INFO)
                    {
                        return ShowWalletInfo(walletDB, vm);
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

                    if (vm.count(cli::NODE_ADDR) == 0)
                    {
                        LOG_ERROR() << "node address should be specified";
                        return -1;
                    }

                    string nodeURI = vm[cli::NODE_ADDR].as<string>();
                    io::Address node_addr;
                    if (!node_addr.resolve(nodeURI.c_str()))
                    {
                        LOG_ERROR() << "unable to resolve node address: " << nodeURI;
                        return -1;
                    }

                    io::Address receiverAddr;
                    Amount amount = 0;
                    Amount fee = 0;
                    WalletID receiverWalletID(Zero);
                    bool isTxInitiator = command == cli::SEND || command == cli::RECEIVE;
                    if (isTxInitiator)
                    {
                        if (vm.count(cli::RECEIVER_ADDR) == 0)
                        {
                            LOG_ERROR() << "receiver's address is missing";
                            return -1;
                        }
                        if (vm.count(cli::AMOUNT) == 0)
                        {
                            LOG_ERROR() << "amount is missing";
                            return -1;
                        }

                        receiverWalletID.FromHex(vm[cli::RECEIVER_ADDR].as<string>());

                        auto signedAmount = vm[cli::AMOUNT].as<double>();
                        if (signedAmount < 0)
                        {
                            LOG_ERROR() << "Unable to send negative amount of coins";
                            return -1;
                        }

                        signedAmount *= Rules::Coin; // convert beams to coins

                        amount = static_cast<ECC::Amount>(std::round(signedAmount));
                        if (amount == 0)
                        {
                            LOG_ERROR() << "Unable to send zero coins";
                            return -1;
                        }

                        fee = vm[cli::FEE].as<beam::Amount>();
                    }

                    bool is_server = (command == cli::LISTEN || vm.count(cli::LISTEN));

                    Wallet wallet{ walletDB, is_server ? Wallet::TxCompletedAction() : [](auto) { io::Reactor::get_Current().stop(); } };
                    if (!coldWallet)
                    {
                        proto::FlyClient::NetworkStd nnet(wallet);
                        nnet.m_Cfg.m_vNodes.push_back(node_addr);
                        nnet.Connect();

                        WalletNetworkViaBbs wnet(wallet, nnet, walletDB);

                        wallet.set_Network(nnet, wnet);
                    }
                    else
                    {
                        struct ColdNetwork : beam::proto::FlyClient::INetwork
                        {
                            void Connect() override {};
                            void Disconnect() override {};
                            void PostRequestInternal(proto::FlyClient::Request&) override {};
                        };

                        ColdNetwork nnet;
                        ColdWalletNetwork wnet(wallet);
                        wallet.set_Network(nnet, wnet);
                    }

                    if (isTxInitiator)
                    {
                        WalletAddress senderAddress = newAddress(walletDB, "");
                        wnet.AddOwnAddress(senderAddress);
                        CoinIDList coinIDs = GetPreselectedCoinIDs(vm);
                        wallet.transfer_money(senderAddress.m_walletID, receiverWalletID, move(amount), move(fee), coinIDs, command == cli::SEND, true);
                    }

                    if (command == cli::CANCEL_TX) 
                    {
                        auto txIdVec = from_hex(vm[cli::TX_ID].as<string>());
                        TxID txId;
                        std::copy_n(txIdVec.begin(), 16, txId.begin());
                        auto tx = walletDB->getTx(txId);

                        if (tx)
                        {
                            if (tx->canCancel())
                            {
                                wallet.cancel_tx(txId);
                            }
                            else
                            {
                                LOG_ERROR() << "Transaction could not be cancelled. Invalid transaction status.";
                            }
                        }
                        else
                        {
                            LOG_ERROR() << "Unknown transaction ID.";
                        }
                    }

                    if (command == cli::WALLET_RESCAN)
                    {
                        wallet.Refresh();
                    }

                    io::Reactor::get_Current().run();
                }
            }
        }
        catch (const AddressExpiredException&)
        {
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

