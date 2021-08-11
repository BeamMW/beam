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
#include <boost/scope_exit.hpp>
#include "shaders_manager.h"
#include "utility/logger.h"

namespace beam::wallet {
    ShadersManager::ShadersManager(beam::wallet::Wallet::Ptr wallet,
                                   beam::wallet::IWalletDB::Ptr walletDB,
                                   beam::proto::FlyClient::INetwork::Ptr nodeNetwork,
                                   std::string appid,
                                   std::string appname)
        : _currentAppId(std::move(appid))
        , _currentAppName(std::move(appname))
        , _wdb(std::move(walletDB))
        , _wallet(std::move(wallet))
    {
        assert(_wdb);
        assert(_wallet);
        assert(nodeNetwork);

        m_pPKdf = _wdb->get_OwnerKdf();
        m_pNetwork = std::move(nodeNetwork);
        m_pHist = &_wdb->get_History();
    }

    void ShadersManager::compileAppShader(const std::vector<uint8_t> &shader)
    {
        if (!_done)
        {
            assert(false);
            throw std::runtime_error("still in shader call");
        }

        if (shader.empty())
        {
            assert(false);
            throw std::runtime_error("empty code buffer in ::Compile");
        }

        auto &resBuffer = m_BodyManager;
        beam::Blob shaderBlob(shader);

        // this throws
        beam::bvm2::Processor::Compile(resBuffer, shaderBlob, ManagerStd::Kind::Manager);
    }

    void ShadersManager::pushRequest(Request newReq)
    {
        if (newReq.unique)
        {
            for (const auto &req: _queue)
            {
                if (req.unique == newReq.unique)
                {
                    return;
                }
            }
        }
        _queue.push(std::move(newReq));
    }

    void ShadersManager::CallShaderAndStartTx(const std::vector<uint8_t>& shader, const std::string &args, unsigned method, uint32_t priority, uint32_t unique, DoneAllHandler doneHandler)
    {
        Request req;
        req.shader   = shader;
        req.args     = args;
        req.method   = method;
        req.doneAll  = doneHandler;
        req.priority = priority;
        req.unique   = unique;
        pushRequest(std::move(req));

        if (_done)
        {
            return nextRequest();
        }

        LOG_INFO () << "shader call is still in progress, request " << args << " queued";
    }

    void ShadersManager::CallShader(const std::vector<uint8_t>& shader, const std::string& args, unsigned method, uint32_t priority, uint32_t unique, DoneCallHandler doneHandler)
    {
        Request req;
        req.shader   = shader;
        req.args     = args;
        req.method   = method;
        req.doneCall = doneHandler;
        req.priority = priority;
        req.unique   = unique;
        pushRequest(std::move(req));

        if (_done)
        {
            return nextRequest();
        }

        LOG_INFO () << "shader call is still in progress, request " << args << " queued";
    }

    void ShadersManager::nextRequest()
    {
        if (_queue.empty())
        {
            return;
        }

        const auto& req = _queue.top();
        if (!req.shader.empty())
        {
            try
            {
                compileAppShader(req.shader);
            }
            catch(std::exception& ex)
            {
                return req.doneCall(boost::none, boost::none, std::string(ex.what()));
            }
        }

        if (m_BodyManager.empty())
        {
            return req.doneCall(boost::none, boost::none, std::string("missing shader code"));
        }

        m_Args.clear();
        if (!req.args.empty())
        {
            std::string temp = req.args;
            AddArgs(&temp.front());
        }

        _done = false;
        StartRun(req.method);
    }

    void ShadersManager::ProcessTxData(const ByteBuffer& buffer, DoneTxHandler doneHandler)
    {
        try
        {
            bvm2::ContractInvokeData invokeData;
            if (!fromByteBuffer(buffer, invokeData))
            {
                throw std::runtime_error("failed to deserialize invoke data");
            }

            std::string sComment = bvm2::getFullComment(invokeData);
            ByteBuffer msg(sComment.begin(), sComment.end());

            if (_currentAppId.empty() && _currentAppName.empty())
            {
                LOG_INFO () << "ShadersManager::ProcessTxData";
            }
            else
            {
                LOG_INFO () << "ShadersManager::ProcessTxData for " << _currentAppId << ", " << _currentAppName;
            }

            auto params = CreateTransactionParameters(TxType::Contract)
                    .SetParameter(TxParameterID::ContractDataPacked, invokeData)
                    .SetParameter(TxParameterID::Message, msg);

            if (!_currentAppId.empty())
            {
                params.SetParameter(TxParameterID::AppID, _currentAppId);
            }

            if(!_currentAppName.empty())
            {
                params.SetParameter(TxParameterID::AppName, _currentAppName);
            }

            auto txid = _wallet->StartTransaction(params);
            return doneHandler(txid, boost::none);
        }
        catch(const std::runtime_error& err)
        {
            std::string error(err.what());
            return doneHandler(boost::none, error);
        }
    }

    void ShadersManager::OnDone(const std::exception *pExc)
    {
        if (_queue.empty())
        {
            LOG_WARNING() << "Queue has been cleared before request completed";
            return;
        }

        BOOST_SCOPE_EXIT_ALL(&, this) {
            this->nextRequest();
        };

        _done = true;
        const auto req = _queue.top();
        _queue.pop();

        if (pExc != nullptr)
        {
            boost::optional<std::string> error = boost::none;
            if (pExc->what() && pExc->what()[0] != 0)
            {
                error = std::string(pExc->what());
            }
            else
            {
                error = std::string("unknown error");
            }

            LOG_INFO() << "Shader Error: " << *error;
            if (req.doneAll)
            {
                return req.doneAll(boost::none, boost::none, error);
            }
            else
            {
                return req.doneCall(boost::none, boost::none, error);
            }
        }

        boost::optional<std::string> result = m_Out.str();
        LOG_INFO () << "Shader result: " << *result;

        if (m_vInvokeData.empty())
        {
            if (req.doneAll)
            {
                return req.doneAll(boost::none, result, boost::none);
            }
            else
            {
                return req.doneCall(boost::none, result, boost::none);
            }
        }

        try
        {
            auto buffer = toByteBuffer(m_vInvokeData);

            if (req.doneCall)
            {
                return req.doneCall(buffer, result, boost::none);
            }

            return ProcessTxData(buffer, [result, allHandler = req.doneAll] (boost::optional<TxID> txid, boost::optional<std::string> error)
            {
                return allHandler(std::move(txid), result, std::move(error));
            });
        }
        catch (std::runtime_error &err)
        {
            std::string error = err.what();
            if (req.doneAll)
            {
                return req.doneAll(boost::none, result, error);
            }
            else
            {
                return req.doneCall(boost::none, result, error);
            }
        }
    }

    IShadersManager::Ptr IShadersManager::CreateInstance(
                beam::wallet::Wallet::Ptr wallet,
                beam::wallet::IWalletDB::Ptr wdb,
                beam::proto::FlyClient::INetwork::Ptr nodeNetwork,
                std::string appid,
                std::string appname)
    {
        return std::make_shared<ShadersManager>(
                std::move(wallet),
                std::move(wdb),
                std::move(nodeNetwork),
                std::move(appid),
                std::move(appname)
                );
    }
}
