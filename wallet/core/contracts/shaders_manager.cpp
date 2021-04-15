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
                                   beam::proto::FlyClient::INetwork::Ptr nodeNetwork)
            : _wdb(walletDB)
            , _wallet(wallet)
    {
        assert(walletDB);
        assert(wallet);

        m_pPKdf = _wdb->get_OwnerKdf();
        m_pNetwork = std::move(nodeNetwork);
        m_pHist = &_wdb->get_History();
    }

    void ShadersManager::CompileAppShader(const std::vector<uint8_t> &shader)
    {
        if (!IsDone())
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

    void ShadersManager::CallShaderAndStartTx(const std::string &args, unsigned method, DoneAllHandler doneHandler)
    {
        if (!IsDone())
        {
            return doneHandler(boost::none, boost::none, std::string("still in shader call"));
        }

        if (m_BodyManager.empty())
        {
            return doneHandler(boost::none, boost::none, std::string("missing shader code"));
        }

        m_Args.clear();
        if (!args.empty())
        {
            std::string temp = args;
            AddArgs(&temp.front());
        }

        _done = false;
        _async = false;
        _doneAll = std::move(doneHandler);

        StartRun(method);
        _async = !_done;
    }

    void ShadersManager::CallShader(const std::string& args, unsigned method, DoneCallHandler doneHandler)
    {
        if (!IsDone())
        {
            return doneHandler(boost::none, boost::none, std::string("still in shader call"));
        }

        if (m_BodyManager.empty())
        {
            return doneHandler(boost::none, boost::none, std::string("missing shader code"));
        }

        m_Args.clear();
        if (!args.empty())
        {
            std::string temp = args;
            AddArgs(&temp.front());
        }

        _done = false;
        _async = false;
        _doneCall = std::move(doneHandler);

        StartRun(method);
        _async = !_done;
    }

     void ShadersManager::ProcessTxData(const ByteBuffer& buffer, DoneTxHandler doneHandler)
    {
        try
        {
            decltype(m_vInvokeData) invokeData;
            if (!fromByteBuffer(buffer, invokeData))
            {
                throw std::runtime_error("failed to deserialize invoke data");
            }

            std::string sComment;
            for (size_t i = 0; i < m_vInvokeData.size(); i++)
            {
                const auto& cdata = m_vInvokeData[i];
                if (i) sComment += "; ";
                sComment += cdata.m_sComment;
            }

            ByteBuffer msg(sComment.begin(), sComment.end());
            auto params = CreateTransactionParameters(TxType::Contract)
                    .SetParameter(TxParameterID::ContractDataPacked, m_vInvokeData)
                    .SetParameter(TxParameterID::Message, msg);

            if (!_currentApp.empty())
            {
                params.SetParameter(TxParameterID::AppID, _currentApp);
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
        auto allHandler = _doneAll;
        auto callHandler = _doneCall;

        _done = true;
        decltype(_doneAll)().swap(_doneAll);
        decltype(_doneCall)().swap(_doneCall);

        if (pExc != nullptr)
        {
            boost::optional<std::string> error = boost::none;
            if (strlen(pExc->what()) > 0)
            {
                error = std::string(pExc->what());
            }
            else
            {
                error = std::string("unknown error");
            }

            LOG_INFO() << "Shader Error: " << *error;
            if (allHandler)
            {
                return allHandler(boost::none, boost::none, error);
            }
            else
            {
                return callHandler(boost::none, boost::none, error);
            }
        }

        boost::optional<std::string> result = m_Out.str();
        LOG_INFO () << "Shader result: " << *result;

        if (m_vInvokeData.empty())
        {
            if (allHandler)
            {
                return allHandler(boost::none, result, boost::none);
            }
            else
            {
                return callHandler(boost::none, result, boost::none);
            }
        }

        try
        {
            auto buffer = toByteBuffer(m_vInvokeData);

            if (callHandler)
            {
                return callHandler(buffer, result, boost::none);
            }

            return ProcessTxData(buffer, [result, allHandler] (boost::optional<TxID> txid, boost::optional<std::string> error)
            {
                return allHandler(std::move(txid), result, std::move(error));
            });
        }
        catch (std::runtime_error &err)
        {
            std::string error = err.what();
            if (allHandler)
            {
                return allHandler(boost::none, result, error);
            }
            else
            {
                return callHandler(boost::none, result, error);
            }
        }
    }

    void ShadersManager::SetCurrentApp(const std::string& appid)
    {
        if (!_currentApp.empty())
        {
            throw std::runtime_error("SetCurrentApp while another app is active");
        }

        _currentApp = appid;
    }

    void ShadersManager::ReleaseCurrentApp(const std::string& appid)
    {
        if (_currentApp != appid)
        {
            throw std::runtime_error("Unexpected AppID in releaseAPP");
        }

        _currentApp = std::string();
    }

    IShadersManager::Ptr IShadersManager::CreateInstance(
                beam::wallet::Wallet::Ptr wallet,
                beam::wallet::IWalletDB::Ptr wdb,
                beam::proto::FlyClient::INetwork::Ptr nodeNetwork)
    {
        return std::make_shared<ShadersManager>(std::move(wallet), std::move(wdb), std::move(nodeNetwork));
    }
}
