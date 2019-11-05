// Copyright 2019 The Beam Team
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

#include "utility/logger.h"
#include "utility/io/timer.h"
#include "utility/io/tcpserver.h"
#include "utility/helpers.h"
#include "nlohmann/json.hpp"

#include "wallet/bitcoin/bitcoin.h"

#include "test_helpers.h"

#include "bitcoin/bitcoin.hpp"

WALLET_TEST_INIT

namespace beam::bitcoin
{
    class Provider : public IElectrumSettingsProvider
    {
    public:
        Provider(const ElectrumSettings& settings)
            : m_settings(settings)
        {
        }

        ~Provider() override
        {

        }

        ElectrumSettings GetElectrumSettings() const override
        {
            return m_settings;
        }

    private:
        ElectrumSettings m_settings;
    };
}

using namespace beam;
using json = nlohmann::json;



void testAddress()
{
    bitcoin::ElectrumSettings settings;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    settings.m_addressVersion = bitcoin::getAddressVersion();

    auto provider = std::make_shared<bitcoin::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    bitcoin::Electrum electrum(*mainReactor, *provider);

#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
    std::set<std::string> addresses
    {
        "16AW2aVqy2gva1hxdtF4xhoSrTQTbGSNvM",
        "1HC9pxNaEHSFWgsiHUDUM3QRETNqBmSQQ9",
        "13V8K8wnLnLfiYaD3JHxTHg2SVTzdJyhgs",
        "1gJnvfcWt9W7sboLWW15yDUzd3idFUUvp",
        "1NGH4WK29VnNdPBJVyS6hx1t9z8Dtx1mUW",
        "1BuUxbUAn84q4bM3uf2k2zDZZ7bzwRP3Hh",
        "1KHdjosUP69aExUMqqq6CLH77bdR5oRoHS",
        "1DFnVotGE4pMv1LiPfw6XQptEqWeYqgzPy",
        "19LLcmPxnFiH7SrsgVXG5M7qWASEQ6cKNh",
        "1MUJs4zcrDpo1UuTMLGTViHDLbhcd66oNK",
        "1Q9RKPvseVrYebiehuXBfsZsrsdMRu6b9y",
        "1HeTXHuqiugtF4gPwLEKQFeT2VQVqa7FsW",
        "12H7k6V9wH4unyFqfUZ6s7pyaNXQQAH7fD",
        "18wjL1B6KNkBaHj8F8w2nnyKTuHrWPoNpC",
        "1KJH4e8Dwvq9CqKB2zFoUsH7tnkB8D7Wos",
        "1N38b6D4mC9R4R9SZztyvbV97uKPfiPpvp",
        "13oWcAxFsHW1n81gj7BVAM1x7qiqhvnjhp",
        "1H4u6j8yZ49FS1Ft2nvXRpRKhWzaJTcCHD",
        "1Lw1ec2Q2KsT2VcV8sXdsr3bjwkaokPE6U",
        "1Ei6D8SRSZ7w4MgDpZsGSvpqcejjqw2ewU",
        "1LjTVPhMx51QUPjUY9aVL2WgNhRzZERHZw",
};
#else
    std::set<std::string> addresses
    {
        "mkgTKdapn48BM8BaMTDSnd1miT1AZSjV7P",
        "mwi781TZ3JsWHoML13BrAxck6SyY2vxW55",
        "mi15cC2m9omvVf3pksGLHCtMJV4hWNi4B8",
        "mgCG5ykbKuaktz5R45UNutRorceRdr4hqi",
        "n2nEMZPzxXDdQVevDYQUXsED1yivmK7bqw",
        "mrRSFeZ9b9W5qhpfdE17ruRtR7ChtfzfvH",
        "myob2rxTC7aq24wyZQoU2FVRybE84CMABE",
        "msmjnryF36Fch7pL7EuUML3D6q7MUKRu68",
        "morHupUwbH9XtZLVQ4VduGLANA2wK1sX2M",
        "n1zGA85bfFG3nbP54uEqKdVYCbJKXwufM2",
        "n4fNcT1rTXHoRiCGRUVZVnnCisE4Ncgeqf",
        "mxAQpLzpXw892BA1euChEArmtV1ChKr8CC",
        "mgo539a8kJWAa5jTP3XUh33JSN87DyMJEV",
        "moTgd4G58QBSMQCjxhuQciBeKttZQ9WyLh",
        "mypEMhDCkxGPywnnkZEBJnVSknLt2DymJT",
        "n2Z5t9J3aDafqXd4HZsMkWhTytv6gqdYpF",
        "miKTuE3EgJwGZEVJSg9rzGEGyqKYafoxHo",
        "mwarPnDxN5aWD7jVkMtuFjdeZWbHDDoZi4",
        "n1Sxwf7NqMJhoc66rSW1hmFvbwMHkzCPhe",
        "muE3WBXQFaZBqU9qY8qeGr3AUeLSoYwJyx",
        "n1FQnSnLm6SfFWD6FiYs9wj1Eh2hYNYNRJ"
    };
#endif

    for (int i = 0; i < 30; i++)
    {
        electrum.getRawChangeAddress([mainReactor, addresses](const bitcoin::IBridge::Error&, const std::string& addr)
        {
            LOG_INFO() << "generated address = " << addr;

            WALLET_CHECK(addresses.find(addr) != addresses.end());
        });
    }
}

void testDumpPrivKey()
{
    bitcoin::ElectrumSettings settings;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    settings.m_addressVersion = bitcoin::getAddressVersion();

    auto provider = std::make_shared<bitcoin::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    bitcoin::Electrum electrum(*mainReactor, *provider);

#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
    electrum.dumpPrivKey("16AW2aVqy2gva1hxdtF4xhoSrTQTbGSNvM", [](const bitcoin::IBridge::Error&, const std::string& privateKey)
    {
        LOG_INFO() << "private key = " << privateKey;

        libbitcoin::wallet::ec_private addressPrivateKey(privateKey, bitcoin::getAddressVersion());
        libbitcoin::wallet::ec_public publicKey(addressPrivateKey.to_public());
        libbitcoin::wallet::payment_address address = publicKey.to_payment_address(bitcoin::getAddressVersion());
        LOG_INFO() << address.encoded();

        WALLET_CHECK(address.encoded() == "16AW2aVqy2gva1hxdtF4xhoSrTQTbGSNvM");
    });
#else
    electrum.dumpPrivKey("mkgTKdapn48BM8BaMTDSnd1miT1AZSjV7P", [](const bitcoin::IBridge::Error&, const std::string& privateKey)
    {
        LOG_INFO() << "private key = " << privateKey;

        libbitcoin::wallet::ec_private addressPrivateKey(privateKey, bitcoin::getAddressVersion());
        libbitcoin::wallet::ec_public publicKey(addressPrivateKey.to_public());
        libbitcoin::wallet::payment_address address = publicKey.to_payment_address(bitcoin::getAddressVersion());
        LOG_INFO() << address.encoded();

        WALLET_CHECK(address.encoded() == "mkgTKdapn48BM8BaMTDSnd1miT1AZSjV7P");
    });
#endif
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);

    testAddress();
    testDumpPrivKey();
    
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}