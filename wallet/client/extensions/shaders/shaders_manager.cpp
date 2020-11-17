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
#include "shaders_manager.h"
#include "utility/logger.h"

ShadersManager::ShadersManager(beam::wallet::IWalletDB::Ptr walletDB, beam::proto::FlyClient::INetwork::Ptr nodeNetwork, IDone& doneHandler)
    : _wdb(walletDB)
    , _doneHandler(doneHandler)
{
    assert(walletDB);
    m_pPKdf = _wdb->get_OwnerKdf();
    m_pNetwork = std::move(nodeNetwork);
    m_pHist = &_wdb->get_History();
}

ShadersManager::~ShadersManager()
{
}

void ShadersManager::Compile(const std::vector<uint8_t>& shader, Kind kind)
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

    auto& resBuffer = kind == Kind::Manager ? m_BodyManager : m_BodyContract;
    beam::Blob shaderBlob(shader);

    // this throws
    beam::bvm2::Processor::Compile(resBuffer, shaderBlob, kind);
}

void ShadersManager::Start(const std::string& args, unsigned method)
{
    if (!IsDone())
    {
        assert(false);
        throw std::runtime_error("still in shader call");
    }

    if (m_BodyManager.empty())
    {
        throw std::runtime_error("missing shader code");
    }

    m_Args.clear();
    _args = args;

    if (!_args.empty())
    {
        AddArgs(&_args.front());
    }

    _done  = false;
    _async = false;
    _error = "";

    StartRun(method);
    _async = !_done;
}

void ShadersManager::OnDone(const std::exception* pExc)
{
    _done  = true;
    _error = pExc ? pExc->what() : "";
    _doneHandler.onShaderDone();
}
