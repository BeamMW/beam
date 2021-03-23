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

    void ShadersManager::Start(const std::string &args, unsigned method, DoneHandler doneHandler)
    {
        if (!IsDone())
        {
            throw std::runtime_error("still in shader call");
        }

        if (m_BodyManager.empty())
        {
            throw std::runtime_error("missing shader code");
        }

        m_Args.clear();
        if (!args.empty())
        {
            std::string temp = args;
            AddArgs(&temp.front());
        }

        _done = false;
        _async = false;
        _doneHandler = std::move(doneHandler);

        StartRun(method);
        _async = !_done;
    }

    void ShadersManager::OnDone(const std::exception *pExc)
    {
        auto handler = _doneHandler;

        _done = true;
        decltype(_doneHandler)().swap(_doneHandler);

        if (pExc != nullptr)
        {
            boost::optional<std::string> error = boost::none;
            if (strlen(pExc->what()) > 0)
            {
                error = std::string(pExc->what());
            } else
            {
                error = std::string("unknown error");
            }

            LOG_INFO() << "Shader Error: " << *error;
            return handler(boost::none, boost::none, error);
        }

        boost::optional<std::string> result = m_Out.str();
        LOG_INFO () << "Shader result: " << *result;

        if (m_vInvokeData.empty())
        {
            return handler(boost::none, result, boost::none);
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

        try
        {
            auto txid = _wallet->StartTransaction(params);
            handler(txid, result, boost::none);
        }
        catch (std::runtime_error &err)
        {
            std::string error = err.what();
            handler(boost::none, result, error);
        }
    }

    IShadersManager::Ptr IShadersManager::CreateInstance(
                beam::wallet::Wallet::Ptr wallet,
                beam::wallet::IWalletDB::Ptr wdb,
                beam::proto::FlyClient::INetwork::Ptr nodeNetwork)
    {
        return std::make_shared<ShadersManager>(std::move(wallet), std::move(wdb), std::move(nodeNetwork));
    }
}
