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

#include "http/http_client.h"
#include "utility/io/timer.h"
#include "utility/helpers.h"
#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"
#include <boost/program_options.hpp>
#include "nlohmann/json.hpp"

#include <iostream>

using namespace beam;
using json = nlohmann::json;

namespace
{
    const char* CREATE = "create";
    const char* REFUND = "refund";
    const char* REDEEM = "redeem";
    const char* RECEIVER_ADDRESS = "receiver_address";
    const char* SENDER_ADDRESS = "sender_address";
    const char* SECRET = "secret";
    const char* AMOUNT = "amount";
    const char* LOCKTIME = "locktime";
    const char* TXID = "tx_id";
    const char* OUTPUTINDEX = "output_index";
    const char* ADDRESS = "address";
    const char* SCRIPT = "script";

    constexpr uint16_t PORT = 12340;
    constexpr uint16_t JSON_FORMAT_INDENT = 4;

    libbitcoin::chain::script AtomicSwapContract(const libbitcoin::short_hash& hashPublicKeyA
        , const libbitcoin::short_hash& hashPublicKeyB
        , int64_t locktime
        , const libbitcoin::data_chunk& secretHash
        , size_t secretSize)
    {
        using namespace libbitcoin::machine;

        operation::list contract_operations;

        contract_operations.emplace_back(operation(opcode::if_)); // Normal redeem path
        {
            // Require initiator's secret to be a known length that the redeeming
            // party can audit.  This is used to prevent fraud attacks between two
            // currencies that have different maximum data sizes.
            contract_operations.emplace_back(operation(opcode::size));
            operation secretSizeOp;
            secretSizeOp.from_string(std::to_string(secretSize));
            contract_operations.emplace_back(secretSizeOp);
            contract_operations.emplace_back(operation(opcode::equalverify));

            // Require initiator's secret to be known to redeem the output.
            contract_operations.emplace_back(operation(opcode::sha256));
            contract_operations.emplace_back(operation(secretHash));
            contract_operations.emplace_back(operation(opcode::equalverify));

            // Verify their signature is being used to redeem the output.  This
            // would normally end with OP_EQUALVERIFY OP_CHECKSIG but this has been
            // moved outside of the branch to save a couple bytes.
            contract_operations.emplace_back(operation(opcode::dup));
            contract_operations.emplace_back(operation(opcode::hash160));
            contract_operations.emplace_back(operation(libbitcoin::to_chunk(hashPublicKeyB)));
        }
        contract_operations.emplace_back(operation(opcode::else_)); // Refund path
        {
            // Verify locktime and drop it off the stack (which is not done by CLTV).
            operation locktimeOp;
            locktimeOp.from_string(std::to_string(locktime));
            contract_operations.emplace_back(locktimeOp);
            contract_operations.emplace_back(operation(opcode::checklocktimeverify));
            contract_operations.emplace_back(operation(opcode::drop));

            // Verify our signature is being used to redeem the output.  This would
            // normally end with OP_EQUALVERIFY OP_CHECKSIG but this has been moved
            // outside of the branch to save a couple bytes.
            contract_operations.emplace_back(operation(opcode::dup));
            contract_operations.emplace_back(operation(opcode::hash160));
            contract_operations.emplace_back(operation(libbitcoin::to_chunk(hashPublicKeyA)));
        }
        contract_operations.emplace_back(operation(opcode::endif));

        // Complete the signature check.
        contract_operations.emplace_back(operation(opcode::equalverify));
        contract_operations.emplace_back(operation(opcode::checksig));

        return libbitcoin::chain::script(contract_operations);
    }

    class RPCClient
    {
    public:
        /// Returns true to keep connection alive, false to close connection
        using OnResponse = std::function<void(const json& reply)>;

        RPCClient(const std::string& host
            , const uint16_t port
            , const std::string& user
            , const std::string& password
            , io::Reactor::Ptr reactor)
            : m_reactor{ !reactor ? io::Reactor::create() : reactor }
            , m_host(host)
            , m_port(port)
            , m_user(user)
            , m_password(password)
        {
            m_httpClient = std::make_unique<HttpClient>(*m_reactor);
        }

        void CallRPC(const std::string& strMethod, const std::vector<std::string>& args, const OnResponse& callback);

        void run() { m_reactor->run(); };
        void stop() { m_reactor->stop(); };

        uint16_t port() const { return m_port; }

    private:

        io::Reactor::Ptr m_reactor;
        std::string m_host;
        uint16_t m_port;
        std::string m_user;
        std::string m_password;
        std::unique_ptr<HttpClient> m_httpClient;
    };

    void RPCClient::CallRPC(const std::string& strMethod, const std::vector<std::string>& args, const OnResponse& callback)
    {
        std::string userWithPass{ m_user + ":" + m_password };

        libbitcoin::data_chunk t(userWithPass.begin(), userWithPass.end());
        std::string auth("Basic " + libbitcoin::encode_base64(t));
        std::string_view authView(auth);
        const HeaderPair headers[] = { {"Authorization", authView.data()} };
        std::string params;
        for (auto& param : args) {
            params += param + ",";
        }
        params.pop_back();

        const std::string requestData(R"({"method":")" + strMethod + R"(","params":[)" + params + "]}");

        io::Address a(io::Address::localhost(), port());

        HttpClient::Request request;
        request.address(a);
        request.connectTimeoutMsec(2000);
        request.pathAndQuery("/");
        request.headers(headers);
        request.numHeaders(1);
        request.method("POST");
        request.body(requestData.data(), requestData.size());
        //request.body(data, strlen(data));

        LOG_INFO() << requestData << "\n";

        request.callback([callback](uint64_t id, const HttpMsgReader::Message& msg) -> bool {
            LOG_INFO() << "response from " << id;
            size_t sz = 0;
            const void* body = msg.msg->get_body(sz);
            if (sz > 0 && body)
            {
                json j = json::parse(std::string(static_cast<const char*>(body), sz));
                LOG_INFO() << j.dump(JSON_FORMAT_INDENT);

                callback(j);
            }
            else
            {
                callback("");
            }
            return false;
        });

        m_httpClient->send_request(request);
    }
}

/*
sender:
1) generate(get) secret string
2) create contract script
3) create contract TX
4) send contract TX to chain, save TX id
 -- refund ---
6) create refund TX with timelock like timelock in contract script
7) if timelock expired, send refund TX to chain
-- redeem beam ---
8) ?

receiver
1) get hash256(secret string) from sender
2) get contractTX id from sender
2) create contract script for audit
3) create redeem TX
*/

class AtomicSwapHelper
{
public:
    AtomicSwapHelper(const std::string& host
        , const uint16_t port
        , const std::string& user
        , const std::string& password
        , io::Reactor::Ptr reactor);
    ~AtomicSwapHelper() = default;

    void CreateContractTX(const std::string& senderAddress
        , const std::string& recieverAddress
        , const libbitcoin::data_chunk& secretHash
        , size_t secretSize
        , int64_t amount
        , int64_t locktime);

    void CreateRefundTx(const std::string& withdrawAddress
        , const std::string& contractTxId
        , int64_t amount
        , int64_t locktime
        , int outputIndex);

    void CreateRedeemTx(const std::string& withdrawAddress
        , const std::string& contractTxId
        , int64_t amount
        , const std::string& secret
        , int outputIndex);

    void SetRedeemScript(libbitcoin::chain::script& script) { m_redeemScript = script; }

private:
    void OnFundRawTransaction(const json& reply);
    void OnSignRawContactTx(const json& reply);
    void OnSendContractTx(const json& reply);

    void OnCreateRawRefundTx(const json& reply);
    void OnDumpSenderPrivateKey(const json& reply);

    void OnCreateRawRedeemTx(const json& reply);
    void OnDumpReceiverPrivateKey(const json& reply);

    io::Reactor::Ptr m_reactor;
    std::unique_ptr<RPCClient> m_rpcClient;

    int m_valuePos = 0;
    float m_fee = 0;
    std::string m_withdrawAddress;
    libbitcoin::chain::transaction m_withdrawTX;
    libbitcoin::chain::script m_redeemScript;
    std::string m_secret;
};

AtomicSwapHelper::AtomicSwapHelper(const std::string& host
    , const uint16_t port
    , const std::string& user
    , const std::string& password
    , io::Reactor::Ptr reactor)
    : m_reactor{ !reactor ? io::Reactor::create() : reactor }
    , m_rpcClient(std::make_unique<RPCClient>(host, port, user, password, m_reactor))
{
}

void AtomicSwapHelper::CreateContractTX(const std::string& senderAddressRaw
    , const std::string& receiverAddressRaw
    , const libbitcoin::data_chunk& secretHash
    , size_t secretSize
    , int64_t amount
    , int64_t locktime)
{
    libbitcoin::wallet::payment_address senderAddress(senderAddressRaw);
    libbitcoin::wallet::payment_address receiverAddress(receiverAddressRaw);

    auto contractScript = AtomicSwapContract(senderAddress.hash(), receiverAddress.hash(), locktime, secretHash, secretSize);

    libbitcoin::chain::transaction contractTx;
    libbitcoin::chain::output output(amount, contractScript);
    contractTx.outputs().push_back(output);

    std::string hexTx = libbitcoin::encode_base16(contractTx.to_data());

    // fundrawtransaction
    m_rpcClient->CallRPC("fundrawtransaction", { "\"" + hexTx + "\"" }, BIND_THIS_MEMFN(OnFundRawTransaction));
}

void AtomicSwapHelper::OnFundRawTransaction(const json& reply)
{
    // Parse reply
    const auto& error = reply["error"];
    const auto& result = reply["result"];
    auto hexTx = result["hex"].get<std::string>();
    int changePos = result["changepos"].get<int>();
    m_fee = result["fee"].get<float>();      // calculate fee!
    m_valuePos = changePos ? 0 : 1;

    LOG_INFO() << "error: " << error.dump(JSON_FORMAT_INDENT) << "\n";
    LOG_INFO() << "result: " << result.dump(JSON_FORMAT_INDENT) << "\n";

    // signrawtransaction
    m_rpcClient->CallRPC("signrawtransactionwithwallet", { "\"" + hexTx + "\"" }, BIND_THIS_MEMFN(OnSignRawContactTx));
}

void AtomicSwapHelper::OnSignRawContactTx(const json& reply)
{
    // Parse reply
    const auto& error = reply["error"];
    const auto& result = reply["result"];
    auto hexTx = result["hex"].get<std::string>();

    LOG_INFO() << "error: " << error.dump(JSON_FORMAT_INDENT) << "\n";
    LOG_INFO() << "result: " << result.dump(JSON_FORMAT_INDENT) << "\n";

    // sendrawtransaction
    m_rpcClient->CallRPC("sendrawtransaction", { "\"" + hexTx + "\"" }, BIND_THIS_MEMFN(OnSendContractTx));
}

void AtomicSwapHelper::OnSendContractTx(const json& reply)
{
    // Parse reply
    const auto& error = reply["error"];
    const auto& result = reply["result"];
    LOG_INFO() << error.dump(JSON_FORMAT_INDENT) << "\n";
    LOG_INFO() << result.dump(JSON_FORMAT_INDENT) << "\n";
}

void AtomicSwapHelper::CreateRefundTx(const std::string& withdrawAddress
    , const std::string& contractTxId
    , int64_t amount
    , int64_t locktime
    , int outputIndex)
{
    m_withdrawAddress = withdrawAddress;
    std::vector<std::string> args;
    args.emplace_back("[{\"txid\": \"" + contractTxId + "\", \"vout\":" + std::to_string(outputIndex) + ", \"Sequence\": " + std::to_string(libbitcoin::max_input_sequence - 1) + " }]");
    args.emplace_back("[{\"" + withdrawAddress + "\": " + std::to_string(double(amount) / libbitcoin::satoshi_per_bitcoin) + "}]");
    args.emplace_back(std::to_string(locktime));

    m_rpcClient->CallRPC("createrawtransaction", args, BIND_THIS_MEMFN(OnCreateRawRefundTx));
}

void AtomicSwapHelper::OnCreateRawRefundTx(const json& reply)
{
    // Parse reply
    const auto& error = reply["error"];
    const auto& result = reply["result"];

    LOG_INFO() << error.dump(JSON_FORMAT_INDENT) << "\n";
    LOG_INFO() << result.dump(JSON_FORMAT_INDENT) << "\n";

    libbitcoin::data_chunk tx_data;
    libbitcoin::decode_base16(tx_data, result.get<std::string>());
    m_withdrawTX = libbitcoin::chain::transaction::factory_from_data(tx_data);

    m_rpcClient->CallRPC("dumpprivkey", { "\"" + m_withdrawAddress + "\"" }, BIND_THIS_MEMFN(OnDumpSenderPrivateKey));    // load private key !
}

void AtomicSwapHelper::OnDumpSenderPrivateKey(const json& reply)
{
    // Parse reply
    const auto& error = reply["error"];
    const auto& result = reply["result"];

    LOG_INFO() << error.dump(JSON_FORMAT_INDENT) << "\n";
    LOG_INFO() << result.dump(JSON_FORMAT_INDENT) << "\n";

    libbitcoin::wallet::ec_private wallet_key(result.get<std::string>(), libbitcoin::wallet::ec_private::testnet_wif);
    libbitcoin::endorsement sig;

    uint32_t input_index = 0;
    libbitcoin::chain::script::create_endorsement(sig, wallet_key.secret(), m_redeemScript, m_withdrawTX, input_index, libbitcoin::machine::sighash_algorithm::all);

    // Create input script
    libbitcoin::machine::operation::list sig_script;
    libbitcoin::ec_compressed pubkey = wallet_key.to_public().point();

    // <my sig> <my pubkey> 0
    sig_script.push_back(libbitcoin::machine::operation(sig));
    sig_script.push_back(libbitcoin::machine::operation(libbitcoin::to_chunk(pubkey)));
    sig_script.push_back(libbitcoin::machine::operation(libbitcoin::machine::opcode(0)));

    libbitcoin::chain::script input_script(sig_script);

    // Add input script to first input in transaction
    m_withdrawTX.inputs()[0].set_script(input_script);

    // sendrawtransaction
    auto hexTx = libbitcoin::encode_base16(m_withdrawTX.to_data());
    m_rpcClient->CallRPC("sendrawtransaction", { "\"" + hexTx + "\"" }, BIND_THIS_MEMFN(OnSendContractTx));
}

void AtomicSwapHelper::CreateRedeemTx(const std::string& withdrawAddress
    , const std::string& contractTxId
    , int64_t amount
    , const std::string& secret
    , int outputIndex)
{
    m_withdrawAddress = withdrawAddress;
    m_secret = secret;

    std::vector<std::string> args;
    args.emplace_back("[{\"txid\": \"" + contractTxId + "\", \"vout\":" + std::to_string(outputIndex) + ", \"Sequence\": " + std::to_string(libbitcoin::max_input_sequence - 1) + " }]");
    args.emplace_back("[{\"" + withdrawAddress + "\": " + std::to_string(double(amount) / libbitcoin::satoshi_per_bitcoin) + "}]");

    m_rpcClient->CallRPC("createrawtransaction", args, BIND_THIS_MEMFN(OnCreateRawRedeemTx));
}

void AtomicSwapHelper::OnCreateRawRedeemTx(const json& reply)
{
    // Parse reply
    const auto& error = reply["error"];
    const auto& result = reply["result"];

    LOG_INFO() << error.dump(JSON_FORMAT_INDENT) << "\n";
    LOG_INFO() << result.dump(JSON_FORMAT_INDENT) << "\n";

    libbitcoin::data_chunk tx_data;
    libbitcoin::decode_base16(tx_data, result.get<std::string>());
    m_withdrawTX = libbitcoin::chain::transaction::factory_from_data(tx_data);

    m_rpcClient->CallRPC("dumpprivkey", { "\"" + m_withdrawAddress + "\"" }, BIND_THIS_MEMFN(OnDumpReceiverPrivateKey));    // load private key !
}

void AtomicSwapHelper::OnDumpReceiverPrivateKey(const json& reply)
{
    // Parse reply
    const auto& error = reply["error"];
    const auto& result = reply["result"];

    LOG_INFO() << error.dump(JSON_FORMAT_INDENT) << "\n";
    LOG_INFO() << result.dump(JSON_FORMAT_INDENT) << "\n";

    libbitcoin::wallet::ec_private wallet_key(result.get<std::string>(), libbitcoin::wallet::ec_private::testnet_wif);
    libbitcoin::endorsement sig;

    uint32_t input_index = 0;
    libbitcoin::chain::script::create_endorsement(sig, wallet_key.secret(), m_redeemScript, m_withdrawTX, input_index, libbitcoin::machine::sighash_algorithm::all);

    // Create input script
    libbitcoin::machine::operation::list sig_script;
    libbitcoin::ec_compressed pubkey = wallet_key.to_public().point();

    // <their sig> <their pubkey> <initiator secret> 1
    sig_script.push_back(libbitcoin::machine::operation(sig));
    sig_script.push_back(libbitcoin::machine::operation(libbitcoin::to_chunk(pubkey)));
    sig_script.push_back(libbitcoin::machine::operation(libbitcoin::to_chunk(m_secret)));
    sig_script.push_back(libbitcoin::machine::operation(libbitcoin::machine::opcode::push_positive_1));

    libbitcoin::chain::script input_script(sig_script);

    // Add input script to first input in transaction
    m_withdrawTX.inputs()[0].set_script(input_script);

    // sendrawtransaction
    auto hexTx = libbitcoin::encode_base16(m_withdrawTX.to_data());
    m_rpcClient->CallRPC("sendrawtransaction", { "\"" + hexTx + "\"" }, BIND_THIS_MEMFN(OnSendContractTx));
}

void testAtomicSwap(const boost::program_options::variables_map& vm)
{
    auto reactor = io::Reactor::create();

    io::Timer::Ptr timer = io::Timer::create(*reactor);
    int x = 30;
    timer->start(0, false, [&] {
        timer->start(100, true, [&] {
            if (--x == 0) {
                reactor->stop();
            }
        });
    });

    auto swap = AtomicSwapHelper("127.0.0.1", 13400, "Bob", "123", reactor);
    //auto swap = AtomicSwapHelper("127.0.0.1", 13300, "Alice", "123", reactor);
    //auto swap = AtomicSwapHelper("127.0.0.1", 18443, "", "", reactor);
    //{
    //    const std::string receiverAddressRaw = "myNs8Tv1D8GfC14QwC3dgH1v5h7Yg2pQHV";
    //    const std::string senderAddressRaw = "mfbzZH33ppZKL28vtnosKSEWNEYmYHSPAV";
    //    const int64_t amount = 6 * libbitcoin::satoshi_per_bitcoin;
    //    const int64_t locktime = 1552550930;
    //    const std::string secret = "secret string!";
    //    const uint16_t outputIndex = 1;

    //    libbitcoin::data_chunk secretHash = libbitcoin::sha256_hash_chunk(libbitcoin::to_chunk(secret));
    //    libbitcoin::wallet::payment_address senderAddress(senderAddressRaw);
    //    libbitcoin::wallet::payment_address receiverAddress(receiverAddressRaw);
    //    auto contractScript = AtomicSwapContract(senderAddress.hash(), receiverAddress.hash(), locktime, secretHash, secret.size());

    //    swap.SetRedeemScript(contractScript);
    //}

    // TODO: get/calculate fee
    const int64_t fee = 20000;

    const std::string secret = vm[SECRET].as<std::string>();
    const libbitcoin::data_chunk secretHash = libbitcoin::sha256_hash_chunk(libbitcoin::to_chunk(secret));
    const std::string receiverAddressRaw = vm[RECEIVER_ADDRESS].as<std::string>();
    const std::string senderAddressRaw = vm[SENDER_ADDRESS].as<std::string>();
    const int64_t amount = vm[AMOUNT].as<int64_t>() * libbitcoin::satoshi_per_bitcoin;
    const int64_t locktime = vm[LOCKTIME].as<int64_t>();

    libbitcoin::wallet::payment_address senderAddress(senderAddressRaw);
    libbitcoin::wallet::payment_address receiverAddress(receiverAddressRaw);
    auto contractScript = AtomicSwapContract(senderAddress.hash(), receiverAddress.hash(), locktime, secretHash, secret.size());

    swap.SetRedeemScript(contractScript);

    if (vm.count(CREATE)) {
        swap.CreateContractTX(senderAddressRaw, receiverAddressRaw, secretHash, secret.size(), amount, locktime);
    }

    if (vm.count(REFUND)) {
        const std::string txId = vm[TXID].as<std::string>();
        const int64_t outputIndex = vm[OUTPUTINDEX].as<uint16_t>();
        const int64_t withdrawAmount = amount - fee;

        swap.CreateRefundTx(senderAddressRaw, txId, withdrawAmount, locktime, outputIndex);
    }

    if (vm.count(REDEEM)) {
        const std::string txId = vm[TXID].as<std::string>();
        const int64_t outputIndex = vm[OUTPUTINDEX].as<uint16_t>();
        const int64_t withdrawAmount = amount - fee;

        swap.CreateRedeemTx(receiverAddressRaw, txId, withdrawAmount, secret, outputIndex);
    }

    //swap.CreateContractTX(senderAddressRaw, receiverAddressRaw, secretHash, secret.size(), amount, locktime);
    //swap.CreateRefundTx(senderAddressRaw, "e38f660f8ceb1c8892a5e0b55e96376baa6c18e770290add1cb6d2608081e904", withdrawAmount, locktime, outputIndex);
    //swap.CreateRedeemTx(senderAddressRaw, "e38f660f8ceb1c8892a5e0b55e96376baa6c18e770290add1cb6d2608081e904", withdrawAmount, secret, outputIndex);

    reactor->run();
}

int main(int argc, char* argv[]) {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);

    /*
     -create senderAddress receiverAddress secretStr amount locktime
     -refund address txID amount locktime outputIndex redeemscript
     -redeem address txID amount secret outputIndex redeemscript
    */

    // Declare the supported options.
    boost::program_options::options_description options("Allowed options");
    options.add_options()
        ("help", "produce help message")
        (CREATE, "description")

        (SENDER_ADDRESS, boost::program_options::value<std::string>(), "description")
        (RECEIVER_ADDRESS, boost::program_options::value<std::string>(), "description")
        (SECRET, boost::program_options::value<std::string>(), "description")
        (AMOUNT, boost::program_options::value<int64_t>(), "description")
        (LOCKTIME, boost::program_options::value<int64_t>(), "description")


        (REFUND, "description")
        (REDEEM, "description")

        (ADDRESS, boost::program_options::value<std::string>(), "description")
        (TXID, boost::program_options::value<std::string>(), "description")
        (OUTPUTINDEX, boost::program_options::value<uint16_t>(), "description")
        (SCRIPT, boost::program_options::value<std::string>(), "description")
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), vm);
    boost::program_options::notify(vm);

    if (vm.count("help")) {
        std::cout << options << "\n";
        return 1;
    }

    if (vm.count(CREATE)) {
        if (!(vm.count(SENDER_ADDRESS) && vm.count(RECEIVER_ADDRESS) && vm.count(SECRET) && vm.count(AMOUNT) && vm.count(LOCKTIME))) {
            // invalid argv
            std::cout << "--create --sender_address 'address' --receiver_address 'address' --secret 'secret' --locktime 'locktime' \n";
            return 0;
        }
    }

    if (vm.count(REFUND)) {
        if (!(vm.count(SENDER_ADDRESS) && vm.count(RECEIVER_ADDRESS) && vm.count(SECRET) && vm.count(LOCKTIME) && vm.count(TXID) && vm.count(OUTPUTINDEX))) {
            // invalid argv
            std::cout << "--refund --sender_address 'address' --receiver_address 'address' --secret 'secret' --locktime 'locktime' --tx_id 'tx_id' --output_index 'output_index'\n";
            return 0;
        }
    }

    if (vm.count(REDEEM)) {
        if (!(vm.count(SENDER_ADDRESS) && vm.count(RECEIVER_ADDRESS) && vm.count(SECRET) && vm.count(TXID) && vm.count(OUTPUTINDEX) && vm.count(LOCKTIME))) {
            // invalid argv
            std::cout << "--redeem --sender_address 'address' --receiver_address 'address' --secret 'secret' --locktime 'locktime' --tx_id 'tx_id' --output_index 'output_index'\n";
            return 0;
        }
    }

    testAtomicSwap(vm);

    return 0;
}