// Copyright 2018-2020 The Beam Team
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
#include "service_client.h"
#include "keykeeper/wasm_key_keeper.h"
#include "wallet/core/simple_transaction.h"
#include "node_connection.h"
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid_generators.hpp>

namespace beam::wallet {
    namespace {
        std::string makeDBPath(const std::string& name)
        {
            const char *DB_FOLDER = "wallets";
            auto path = boost::filesystem::system_complete(DB_FOLDER);

            if (!boost::filesystem::exists(path))
            {
                boost::filesystem::create_directories(path);
            }

            std::string fname = std::string(name) + ".db";
            path /= fname;
            return path.string();
        }

        std::string generateWalletID(Key::IPKdf::Ptr ownerKdf)
        {
            Key::ID kid(Zero);
            kid.m_Type = ECC::Key::Type::WalletID;

            ECC::Point::Native pt;
            ECC::Hash::Value hv;
            kid.get_Hash(hv);
            ownerKdf->DerivePKeyG(pt, hv);
            PeerID pid;
            pid.Import(pt);
            return pid.str();
        }

        static std::string generateUid()
        {
            std::array<uint8_t, 16> buf{};
            {
                boost::uuids::uuid uid = boost::uuids::random_generator()();
                std::copy(uid.begin(), uid.end(), buf.begin());
            }

            return to_hex(buf.data(), buf.size());
        }
    }

    ServiceClientHandler::ServiceClientHandler(bool withAssets, const io::Address& nodeAddr, io::Reactor::Ptr reactor, WebSocketServer::SendFunc wsSend, WalletMap& walletMap)
        : WalletServiceApi(static_cast<WalletApiHandler::IWalletData&>(*this))
        , _reactor(std::move(reactor))
        , _walletMap(walletMap)
        , _nodeAddr(nodeAddr)
        , _withAssets(withAssets)
        , _wsSend(std::move(wsSend))
    {
    }

    ServiceClientHandler::~ServiceClientHandler() noexcept
    {
        try
        {
            // _walletDB and _wallet should be destroyed in the context of _reactor
            if (_walletDB || _wallet)
            {
                auto holder = std::make_shared<io::AsyncEvent::Ptr>();
                *holder = io::AsyncEvent::create(*_reactor,
                    [holder, walletDB = std::move(_walletDB), wallet = std::move(_wallet)]() mutable
                    {
                        holder.reset();
                    });
                (*holder)->post();
            }
        }
        catch (...)
        {
        }
    }

    void ServiceClientHandler::onWSDataReceived(const std::string& data)
    {
        // Something came through websocket
        try
        {
            json msg = json::parse(data.c_str(), data.c_str() + data.size());

            if (WalletApi::existsJsonParam(msg, "result"))
            {
                if (_keeperCallbacks.empty())
                    return;

                _keeperCallbacks.front()(msg["result"]);
                _keeperCallbacks.pop();
            }
            else if (WalletApi::existsJsonParam(msg, "error"))
            {
                const auto& error = msg["error"];
                LOG_ERROR() << "JSON RPC error id: " << error["id"] << " message: " << error["message"];
            }
            else
            {
                // !TODO: don't forget to cache this request
                WalletServiceApi::parse(data.c_str(), data.size());
            }
        }
        catch (const nlohmann::detail::exception & e)
        {
            LOG_ERROR() << "json parse: " << e.what() << "\n";
        }
    }

    void ServiceClientHandler::serializeMsg(const json& msg)
    {
        socketSend(msg);
    }

    void ServiceClientHandler::socketSend(const std::string& data)
    {
        _wsSend(data);
    }

    void ServiceClientHandler::socketSend(const json& msg)
    {
        socketSend(msg.dump());

        const char* jsonError = "error";
        const char* jsonCode  = "code";

        if (existsJsonParam(msg, jsonError))
        {
            const auto& error = msg[jsonError];
            if (existsJsonParam(error, jsonCode))
            {
                const auto &code = error[jsonCode];
                if (code.is_number_integer() && code == ApiError::ThrottleError)
                {
                    int a = 0;
                    a++;
                }
            }
        }
    }

    void ServiceClientHandler::onWalletApiMessage(const JsonRpcId& id, const CreateWallet& data)
    {
        try
        {
            LOG_DEBUG() << "CreateWallet(id = " << id << ")";

            beam::KeyString ks;

            ks.SetPassword(data.pass);
            ks.m_sRes = data.ownerKey;

            std::shared_ptr<ECC::HKdfPub> ownerKdf = std::make_shared<ECC::HKdfPub>();

            if (ks.Import(*ownerKdf))
            {
                auto keyKeeper = createKeyKeeper(ownerKdf);
                auto dbName = generateWalletID(ownerKdf);
                IWalletDB::Ptr walletDB = WalletDB::init(makeDBPath(dbName), SecString(data.pass), keyKeeper);

                if (walletDB)
                {
                    _walletMap[dbName] = WalletInfo({}, walletDB);
                    // generate default address
                    WalletAddress address;
                    walletDB->createAddress(address);
                    address.m_label = "default";
                    walletDB->saveAddress(address);

                    sendApiResponse(id, CreateWallet::Response{dbName});
                    return;
                }
            }

            WalletServiceApi::doError(id, ApiError::InternalErrorJsonRpc, "Wallet not created.");
        }
        catch (const DatabaseException& ex)
        {
             WalletServiceApi::doError(id, ApiError::DatabaseError, ex.what());
        }
    }

    void ServiceClientHandler::onWalletApiMessage(const JsonRpcId &id, const OpenWallet &data)
    {
        LOG_DEBUG() << "OpenWallet(id = " << id << ")";

        try
        {
            const auto openWallet = [&]() {
                _walletDB = WalletDB::open(makeDBPath(data.id), SecString(data.pass), createKeyKeeperFromDB(data.id, data.pass));
                _wallet = std::make_shared<Wallet>(_walletDB, _withAssets);
            };

            auto it = _walletMap.find(data.id);
            if (it == _walletMap.end())
            {
                openWallet();
            }
            else if (auto wdb = it->second.walletDB.lock(); wdb)
            {
                _walletDB = wdb;
                _wallet = it->second.wallet.lock();
            }
            else
            {
                openWallet();
            }

            if (!_walletDB)
            {
                WalletServiceApi::doError(id, ApiError::InternalErrorJsonRpc, "Wallet not opened.");
                return;
            }

            _walletMap[data.id].walletDB = _walletDB;
            _walletMap[data.id].wallet = _wallet;
            LOG_DEBUG() << "Wallet sucessfully opened, wallet id " << data.id;

            _wallet->ResumeAllTransactions();
            if (data.freshKeeper) {
                // We won't be able to sign, nonces are regenerated
                _wallet->VisitActiveTransaction([&](const TxID& txid, BaseTransaction::Ptr tx) {
                   if (tx->GetType() == TxType::Simple)
                   {
                       SimpleTransaction::State state = SimpleTransaction::State::Initial;
                       if (tx->GetParameter(TxParameterID::State, state))
                       {
                           if (state < SimpleTransaction::State::Registration)
                           {
                               LOG_DEBUG() << "Fresh keykeeper transaction cancel, txid " << txid << " , state " << state;
                               _wallet->CancelTransaction(txid);
                           }
                       }
                   }
                });
            }

            auto nnet = std::make_shared<ServiceNodeConnection>(*_wallet);
            nnet->m_Cfg.m_PollPeriod_ms = 0;//options.pollPeriod_ms.value;

            if (nnet->m_Cfg.m_PollPeriod_ms)
            {
                LOG_INFO() << "Node poll period = " << nnet->m_Cfg.m_PollPeriod_ms << " ms";
                uint32_t timeout_ms = std::max(Rules::get().DA.Target_s * 1000, nnet->m_Cfg.m_PollPeriod_ms);
                if (timeout_ms != nnet->m_Cfg.m_PollPeriod_ms)
                {
                    LOG_INFO() << "Node poll period has been automatically rounded up to block rate: "
                               << timeout_ms << " ms";
                }
            }
            uint32_t responceTime_s = Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
            if (nnet->m_Cfg.m_PollPeriod_ms >= responceTime_s * 1000)
            {
                LOG_WARNING() << "The \"--node_poll_period\" parameter set to more than "
                              << uint32_t(responceTime_s / 3600) << " hours may cause transaction problems.";
            }
            nnet->m_Cfg.m_vNodes.push_back(_nodeAddr);
            nnet->Connect();

            auto wnet = std::make_shared<WalletNetworkViaBbs>(*_wallet, nnet, _walletDB);
            _wallet->AddMessageEndpoint(wnet);
            _wallet->SetNodeEndpoint(nnet);

            // !TODO: not sure, do we need this id in the future
            auto session = generateUid();
            sendApiResponse(id, OpenWallet::Response{session});
        }
        catch(const DatabaseNotFoundException& ex)
        {
            WalletServiceApi::doError(id, ApiError::DatabaseNotFound, ex.what());
        }
        catch(const DatabaseException& ex)
        {
            WalletServiceApi::doError(id, ApiError::DatabaseError, ex.what());
        }
    }

    void ServiceClientHandler::onWalletApiMessage(const JsonRpcId& id, const wallet::Ping& data)
    {
        LOG_DEBUG() << "Ping(id = " << id << ")";
        sendApiResponse(id, wallet::Ping::Response{});
    }

    void ServiceClientHandler::onWalletApiMessage(const JsonRpcId& id, const Release& data)
    {
        LOG_DEBUG() << "Release(id = " << id << ")";
        sendApiResponse(id, Release::Response{});
    }

    void ServiceClientHandler::onWalletApiMessage(const JsonRpcId& id, const CalcChange& data)
    {
        LOG_DEBUG() << "CalcChange(id = " << id << ")";

        auto coins = _walletDB->selectCoins(data.amount, Zero);
        Amount sum = 0;
        for (auto& c : coins)
        {
            sum += c.m_ID.m_Value;
        }

        Amount change = (sum > data.amount) ? (sum - data.amount) : 0UL;
        sendApiResponse(id, CalcChange::Response{change});
    }

    void ServiceClientHandler::onWalletApiMessage(const JsonRpcId& id, const ChangePassword& data)
    {
        LOG_DEBUG() << "ChangePassword(id = " << id << ")";
        _walletDB->changePassword(data.newPassword);
        sendApiResponse(id, ChangePassword::Response{ });
    }

    IPrivateKeyKeeper2::Ptr ServiceClientHandler::createKeyKeeper(const std::string& pass, const std::string& ownerKey)
    {
        beam::KeyString ks;

        ks.SetPassword(pass);
        ks.m_sRes = ownerKey;

        std::shared_ptr<ECC::HKdfPub> ownerKdf = std::make_shared<ECC::HKdfPub>();
        if (ks.Import(*ownerKdf))
        {
            return createKeyKeeper(ownerKdf);
        }

        return {};
    }

    IPrivateKeyKeeper2::Ptr ServiceClientHandler::createKeyKeeperFromDB(const std::string& id, const std::string& pass)
    {
        auto walletDB = WalletDB::open(makeDBPath(id), SecString(pass));
        Key::IPKdf::Ptr pKey = walletDB->get_OwnerKdf();
        return createKeyKeeper(pKey);
    }

    IPrivateKeyKeeper2::Ptr ServiceClientHandler::createKeyKeeper(Key::IPKdf::Ptr ownerKdf)
    {
        return std::make_shared<WasmKeyKeeperProxy>(ownerKdf, *this);
    }
}