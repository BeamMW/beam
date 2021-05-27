// Copyright 2018-2021 The Beam Team
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

#ifndef HOST_BUILD
#define HOST_BUILD
#endif

#include "ethash_utils.h"
#include "core/block_crypt.h"
#include "utility/cli/options.h"
#include "utility/io/reactor.h"
#include "utility/logger.h"
#include "utility/hex.h"
#include "utility/byteorder.h"

#include <boost/filesystem.hpp>

#include "wallet/api/cli/api_server.h"
#include "wallet/api/v6_0/api_base.h"

#include "shaders_ethash.h"

using namespace beam;
namespace fs = boost::filesystem;
using json = nlohmann::json;
namespace
{
    const char* PROVER = "prover";
    const char* DATA_PATH = "path";
    const char* GENERATE = "generate";

    struct Options
    {
        bool        useHttp = false;
        uint16_t    port = 0;
        bool        useAcl = false;
        std::string aclPath;
        ApiServer::TlsOptions tlsOptions;
    };

    struct MyOptions : Options
    {
        std::string dataPath;
    };


    
    int GenerateLocalData(const MyOptions& options)
    {
        fs::path path(Utf8toUtf16(options.dataPath));
        if (!fs::exists(path))
        {
            fs::create_directories(path);
        }

        // 1. Create local data for all the epochs (VERY long)

        ExecutorMT_R exec;

        for (uint32_t iEpoch = 0; iEpoch < 1024; iEpoch++)
        {
            struct MyTask :public Executor::TaskAsync
            {
                uint32_t m_iEpoch;
                std::string m_Path;

                void Exec(Executor::Context&) override
                {
                    EthashUtils::GenerateLocalData(m_iEpoch, (m_Path + ".cache").c_str(), (m_Path + ".tre3").c_str(), 3); // skip 1st 3 levels, size reduction of 2^3 == 8
                   // EthashUtils::CropLocalData((m_Path + ".tre5").c_str(), (m_Path + ".tre3").c_str(), 2); // skip 2 more levels
                }
            };

            auto pTask = std::make_unique<MyTask>();
            pTask->m_iEpoch = iEpoch;
            pTask->m_Path = (path / std::to_string(iEpoch)).string();
            exec.Push(std::move(pTask));
        }
        exec.Flush(0);

        // 2. Generate the 'SuperTree'
        path.append("/");
        EthashUtils::GenerateSuperTree((path / + "Super.tre").string().c_str(), path.string().c_str(), path.string().c_str(), 3);

        return 0;
    }

    using wallet::JsonRpcId;

#define BEAM_ETHASH_SERVICE_API_METHODS(macro) \
    macro(GetProof,           "get_proof",                 API_READ_ACCESS)  
    
    struct GetProof 
    {
        Shaders::Ethash::Hash512 hvSeed;
        uint32_t epoch = 0;
        struct Response
        {
            uint32_t   datasetCount = 0;
            ByteBuffer proof;
        };
    };

    class ProverApi : public wallet::ApiBase
    {
    public:
        ProverApi(wallet::IWalletApiHandler& handler, ACL acl, std::string appid, std::string appname, std::string& dataPath)
            : wallet::ApiBase(handler, std::move(acl), std::move(appid), std::move(appname))
            , m_DataPath(std::move(dataPath))
        {
            if (!m_DataPath.empty() && m_DataPath.back() != '\\' && m_DataPath.back() != '/')
            {
                m_DataPath.push_back('/');
            }
#define REG_FUNC(api, name, writeAccess)    \
        _methods[name] = {                                                \
            [this] (const JsonRpcId &id, const json &msg) {               \
                auto parseRes = onParse##api(id, msg);                    \
                onHandle##api(id, parseRes.first);                        \
            },                                                            \
            [this] (const JsonRpcId &id, const json &msg) -> MethodInfo { \
                auto parseRes = onParse##api(id, msg);                    \
                return parseRes.second;                                   \
            },                                                            \
            writeAccess, false, false                                     \
        };
        BEAM_ETHASH_SERVICE_API_METHODS(REG_FUNC)
#undef REG_FUNC
        }

        BEAM_ETHASH_SERVICE_API_METHODS(BEAM_API_RESPONSE_FUNC)
        BEAM_ETHASH_SERVICE_API_METHODS(BEAM_API_HANDLE_FUNC)
        BEAM_ETHASH_SERVICE_API_METHODS(BEAM_API_PARSE_FUNC)

        template<typename T>
        void doResponse(const JsonRpcId& id, const T& response)
        {
            json msg;
            getResponse(id, response, msg);
            _handler.sendAPIResponse(msg);
        }
    private:
        std::string m_DataPath;
    };

    class ProverApiServer : public ApiServer
    {
        using ApiServer::ApiServer;

        std::unique_ptr<wallet::IWalletApi> createApiInstance(const std::string& version, wallet::IWalletApiHandler& handler) override
        {
            return std::make_unique<ProverApi>(handler, _acl, "", "", m_DataPath);
        }

        std::string m_DataPath;
    };

    void ProverApi::getResponse(const JsonRpcId& id, const GetProof::Response& data, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"dataset_count", data.datasetCount},
                    {"proof", beam::to_hex(data.proof.data(), data.proof.size())}
                }
            }
        };
    }

    void ProverApi::onHandleGetProof(const JsonRpcId& id, const GetProof& data)
    {
        GetProof::Response res;
        std::string sEpoch = std::to_string(data.epoch);
        res.datasetCount = beam::EthashUtils::GenerateProof(
            data.epoch,
            (m_DataPath + sEpoch + ".cache").c_str(),
            (m_DataPath + sEpoch + ".tre5").c_str(),
            (m_DataPath + "Super.tre").c_str(),
            data.hvSeed, res.proof);
        doResponse(id, res);
    }

    std::pair<GetProof, wallet::IWalletApi::MethodInfo> ProverApi::onParseGetProof(const JsonRpcId & id, const json & msg)
    {
        GetProof data;
        data.epoch = ProverApi::getMandatoryParam<uint32_t>(msg, "epoch");
        std::string strSeed = ProverApi::getMandatoryParam<wallet::NonEmptyString>(msg, "seed");
        auto buffer = from_hex(strSeed);
        if (buffer.size() > sizeof(data.hvSeed))
        {
            throw wallet::jsonrpc_exception(wallet::ApiError::InvalidParamsJsonRpc, "Failed to parse seed data");
        }
        data.hvSeed = Blob(&buffer[0], (uint32_t)buffer.size());
        return std::make_pair(data, MethodInfo());
    }

    int RunProver(const Options& options)
    {
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Address listenTo = io::Address().port(options.port);
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);
        ProverApiServer server(std::string("0.0.1"), *reactor, listenTo, options.useHttp, (options.useAcl ? loadACL(options.aclPath) : wallet::IWalletApi::ACL()), options.tlsOptions, {});

        reactor->run();
        return 0;
    }
}

int main(int argc, char* argv[])
{
    const auto path = boost::filesystem::system_complete("./logs");
    auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, "", path.string());
    MyOptions options;
    po::options_description desc("Ethash service options");
    desc.add_options()
        (cli::HELP_FULL, "list of all options")
        (PROVER, "run prover server")
        (GENERATE, "create local data for all the epochs (VERY long)")
        (DATA_PATH, po::value<std::string>(&options.dataPath)->default_value("EthEpoch"), "directory for generated data")
        (cli::PORT_FULL, po::value<uint16_t>(&options.port)->default_value(10000), "port to start prover server on")
        (cli::API_USE_HTTP, po::value<bool>(&options.useHttp)->default_value(false), "use JSON RPC over HTTP")
    ;

    po::options_description authDesc("User authorization options");
    authDesc.add_options()
        (cli::API_USE_ACL, po::value<bool>(&options.useAcl)->default_value(false), "use Access Control List (ACL)")
        (cli::API_ACL_PATH, po::value<std::string>(&options.aclPath)->default_value("prover-api.acl"), "path to ACL file")
        ;

    po::options_description tlsDesc("TLS protocol options");
    tlsDesc.add_options()
        (cli::API_USE_TLS, po::value<bool>(&options.tlsOptions.use)->default_value(false), "use TLS protocol")
        (cli::API_TLS_CERT, po::value<std::string>(&options.tlsOptions.certPath)->default_value("wallet_api.crt"), "path to TLS certificate")
        (cli::API_TLS_KEY, po::value<std::string>(&options.tlsOptions.keyPath)->default_value("wallet_api.key"), "path to TLS private key")
        (cli::API_TLS_REQUEST_CERTIFICATE, po::value<bool>(&options.tlsOptions.requestCertificate)->default_value("false"), "request client's certificate for verification")
        (cli::API_TLS_REJECT_UNAUTHORIZED, po::value<bool>(&options.tlsOptions.rejectUnauthorized)->default_value("true"), "server will reject any connection which is not authorized with the list of supplied CAs.")
        ;

    desc.add(authDesc);
    desc.add(tlsDesc);

    po::variables_map vm;

    po::store(po::command_line_parser(argc, argv)
        .options(desc)
        .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
        .run(), vm);

    vm.notify();

    if (vm.count(cli::HELP))
    {
        std::cout << desc << std::endl;
        return 0;
    }
    if (vm.count(GENERATE))
    {
        return GenerateLocalData(options);
    }
    if (vm.count(PROVER))
    {
        return RunProver(options);
    }

    return 0;
}