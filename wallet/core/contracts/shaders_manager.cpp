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

    struct ManagerStdInWallet::SlotName
    {
#define SLOT_PREFIX_NAME "app_sh_slot_"
        char m_sz[_countof(SLOT_PREFIX_NAME) + 10];

        SlotName(uint32_t iSlot)
        {
            memcpy(m_sz, SLOT_PREFIX_NAME, sizeof(SLOT_PREFIX_NAME) - sizeof(char));
            beam::utoa(m_sz + _countof(SLOT_PREFIX_NAME) - 1, iSlot);
        }
    };

    ManagerStdInWallet::ManagerStdInWallet(Wallet& wallet)
        :m_Wallet(wallet)
    {
        m_pPKdf = wallet.get_WalletDB()->get_OwnerKdf();
        m_pHist = &wallet.get_WalletDB()->get_History();

        m_pNetwork = wallet.GetNodeEndpoint();
        assert(m_pNetwork);
    }

    ManagerStdInWallet::~ManagerStdInWallet()
    {
        m_Comms.m_Map.Clear(); // must be cleared before our d'tor ends, not in the base d'tor
    }

    void ManagerStdInWallet::set_Privilege(uint32_t n)
    {
        m_Privilege = n;
        if (n)
            m_pKdf = m_Wallet.get_WalletDB()->get_MasterKdf();
    }

    bool ManagerStdInWallet::SlotLoad(ECC::Hash::Value& hv, uint32_t iSlot)
    {
        SlotName sn(iSlot);
        return m_Wallet.get_WalletDB()->getVarRaw(sn.m_sz, hv.m_pData, hv.nBytes);
    }

    void ManagerStdInWallet::SlotSave(const ECC::Hash::Value& hv, uint32_t iSlot)
    {
        SlotName sn(iSlot);
        m_Wallet.get_WalletDB()->setVarRaw(sn.m_sz, hv.m_pData, hv.nBytes);
    }

    void ManagerStdInWallet::SlotErase(uint32_t iSlot)
    {
        SlotName sn(iSlot);
        m_Wallet.get_WalletDB()->removeVarRaw(sn.m_sz);
    }

    struct ManagerStdInWallet::Channel
        :public ManagerStd::Comm::Channel
    {
        ManagerStdInWallet* m_pThis = nullptr;
        WalletID m_Wid;
        uint8_t m_Y;

        virtual ~Channel()
        {
            if (m_pThis)
                m_pThis->m_Wallet.Unlisten(m_Wid, &m_Handler);
        }

        struct Handler
            :public IRawCommGateway::IHandler
        {
            void OnMsg(const Blob& msg) override
            {
                auto& c = get_ParentObj();
                c.m_pThis->Comm_OnNewMsg(msg, c);
            }

            IMPLEMENT_GET_PARENT_OBJ(Channel, m_Handler)
        } m_Handler;
    };

    void ManagerStdInWallet::TestCommAllowed() const
    {
        Exc::Test(m_Privilege >= 2);
    }

    void ManagerStdInWallet::Comm_CreateListener(Comm::Channel::Ptr& pRes, const ECC::Hash::Value& hv)
    {
        TestCommAllowed();

        ECC::Scalar::Native sk;
        get_Sk(sk, hv); // would fail if not in privileged mode

        auto pCh = std::make_unique<Channel>();
        auto& c = *pCh;

        c.m_Y = !c.m_Wid.m_Pk.FromSk(sk);
        c.m_Wid.SetChannelFromPk();

        c.m_pThis = this;
        m_Wallet.Listen(c.m_Wid, sk, &c.m_Handler);

        pRes = std::move(pCh);
    }

    void ManagerStdInWallet::Comm_Send(const ECC::Point& pk, const Blob& msg)
    {
        TestCommAllowed();


        WalletID wid;
        wid.m_Pk = pk.m_X;
        wid.SetChannelFromPk();

        m_Wallet.Send(wid, msg);
    }

    void ManagerStdInWallet::WriteStream(const Blob& b, uint32_t iStream)
    {
        const char* szPrefix = "";
        switch (iStream)
        {
        case Shaders::Stream::Out: szPrefix = "App out: "; break;
        case Shaders::Stream::Error: szPrefix = "App error: "; break;
        default:
            return;
        }

        std::cout << szPrefix;
        std::cout.write((const char*) b.p, b.n);
        std::cout << std::endl;
    }

    bvm2::ContractInvokeData ManagerStdInWallet::get_InvokeData()
    {
        bvm2::ContractInvokeData res;
        Cast::Down<bvm2::ContractInvokeDataBase>(res) = std::move(m_InvokeData);
        m_InvokeData.Reset();

        if (res.HasDependent() && !res.HasMultiSig())
        {
            res.m_vec.front().m_Flags |= bvm2::ContractInvokeEntry::Flags::SaveAppInvoke;
            res.m_AppInvoke.m_App = m_BodyManager;
            res.m_AppInvoke.m_Contract = m_BodyContract;
            res.m_AppInvoke.m_Args = m_Args;
            res.m_AppInvoke.m_Privilege = m_Privilege;

            if (!m_fmSpendMax.empty())
            {
                res.m_vec.front().m_Flags |= bvm2::ContractInvokeEntry::Flags::SaveSpendMax;
                res.m_SpendMax = std::move(m_fmSpendMax);
            }
        }

        m_fmSpendMax.clear();

        return res;
    }

    ShadersManager::ShadersManager(Wallet& wallet, std::string appid, std::string appname, uint32_t privilegeLvl)
        : ManagerStdInWallet(wallet)
        , _currentAppId(std::move(appid))
        , _currentAppName(std::move(appname))
    {
        _logResult = appid.empty();
        set_Privilege(privilegeLvl);
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

    void ShadersManager::CallShaderAndStartTx(std::vector<uint8_t>&& shader, std::string&& args, unsigned method, uint32_t priority, uint32_t unique, DoneAllHandler doneHandler)
    {
        Request req;
        req.shader   = std::move(shader);
        req.args     = std::move(args);
        req.method   = method;
        req.doneAll  = std::move(doneHandler);
        req.priority = priority;
        req.unique   = unique;
        pushRequest(std::move(req));

        if (_done)
        {
            return nextRequest();
        }

        BEAM_LOG_VERBOSE() << "shader call is still in progress, request " << args << " queued";
    }

    void ShadersManager::CallShader(std::vector<uint8_t>&& shader, std::string&& args, unsigned method, uint32_t priority, uint32_t unique, DoneCallHandler doneHandler)
    {
        Request req;
        req.shader   = std::move(shader);
        req.args     = std::move(args);
        req.method   = method;
        req.doneCall = std::move(doneHandler);
        req.priority = priority;
        req.unique   = unique;
        pushRequest(std::move(req));

        if (_done)
        {
            return nextRequest();
        }

        BEAM_LOG_VERBOSE () << "shader call is still in progress, request " << args << " queued";
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
                BOOST_SCOPE_EXIT_ALL(&, this) {
                    _queue.pop();
                };
                return req.doneCall(boost::none, boost::none, std::string(ex.what()));
            }
        }

        if (m_BodyManager.empty())
        {
            BOOST_SCOPE_EXIT_ALL(&, this) {
                _queue.pop();
            };
            return req.doneCall(boost::none, boost::none, std::string("missing shader code"));
        }

        m_Args.clear();
        if (!req.args.empty())
        {
            AddArgs(req.args);
        }

        _done = false;
        _startEvent = io::AsyncEvent::create(io::Reactor::get_Current(),
            [this, method = req.method]()
            {
                StartRun(method);
            });

        m_Wallet.DoInSyncedWallet([wp = std::weak_ptr(_startEvent)]()
            {
                if (auto sp = wp.lock())
                    sp->post();
            });
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

            std::string sComment = invokeData.get_FullComment();
            ByteBuffer msg(sComment.begin(), sComment.end());

            if (_currentAppId.empty() && _currentAppName.empty())
            {
                BEAM_LOG_INFO () << "ShadersManager::ProcessTxData";
            }
            else
            {
                BEAM_LOG_INFO () << "ShadersManager::ProcessTxData for " << _currentAppId << ", " << _currentAppName;
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

            auto txid = m_Wallet.StartTransaction(params);
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
            BEAM_LOG_WARNING() << "Queue has been cleared before request completed";
            return;
        }

        BOOST_SCOPE_EXIT_ALL(&, this) {
            _queue.pop();
            this->nextRequest();
        };

        _done = true;
        const auto& req = _queue.top();

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

            BEAM_LOG_INFO() << "Shader Error: " << *error;
            if (req.doneAll)
            {
                return req.doneAll(boost::none, boost::none, std::move(error));
            }
            else
            {
                return req.doneCall(boost::none, boost::none, std::move(error));
            }
        }

        boost::optional<std::string> result = m_Out.str();
        if (_logResult)
        {
            BEAM_LOG_VERBOSE () << "Shader result: " << std::string_view(result ? *result : std::string()).substr(0, 200);
        }

        if (m_InvokeData.m_vec.empty())
        {
            if (req.doneAll)
            {
                return req.doneAll(boost::none, std::move(result), boost::none);
            }
            else
            {
                return req.doneCall(boost::none, std::move(result), boost::none);
            }
        }

        try
        {
            auto buffer = toByteBuffer(get_InvokeData());

            if (req.doneCall)
            {
                return req.doneCall(std::move(buffer), std::move(result), boost::none);
            }

            return ProcessTxData(buffer, [result=std::move(result), allHandler = std::move(req.doneAll)](const boost::optional<TxID>& txid, boost::optional<std::string>&& error) mutable
            {
                return allHandler(txid, std::move(result), std::move(error));
            });
        }
        catch (std::runtime_error &err)
        {
            std::string error = err.what();
            if (req.doneAll)
            {
                return req.doneAll(boost::none, std::move(result), std::move(error));
            }
            else
            {
                return req.doneCall(boost::none, std::move(result), std::move(error));
            }
        }
    }

    IShadersManager::Ptr IShadersManager::CreateInstance(Wallet& wallet, std::string appid, std::string appname,uint32_t privilegeLvl)
    {
        return std::make_shared<ShadersManager>(wallet, std::move(appid), std::move(appname), privilegeLvl);
    }
}
