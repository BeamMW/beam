#include <iostream>
#include "test_helpers.h"
#include "wallet/transactions/swaps/bridges/ethereum/rpc_endpoint.h"
#include "wallet/transactions/swaps/bridges/ethereum/settings.h"

WALLET_TEST_INIT

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

using namespace beam::ethereum;

namespace
{
RpcEndpoint parseOk(const std::string& url)
{
    RpcEndpoint ep;
    WALLET_CHECK(ParseEthereumRpcUrl(url, ep));
    return ep;
}

void checkReject(const std::string& url)
{
    RpcEndpoint ep;
    WALLET_CHECK(!ParseEthereumRpcUrl(url, ep));
}
}

void TestAccepts()
{
    auto ep = parseOk("https://mainnet.infura.io/v3/abcdef0123456789");
    WALLET_CHECK(ep.m_ssl);
    WALLET_CHECK(ep.m_host == "mainnet.infura.io");
    WALLET_CHECK(ep.m_port == 443);
    WALLET_CHECK(ep.m_pathAndQuery == "/v3/abcdef0123456789");

    ep = parseOk("http://localhost:8545");
    WALLET_CHECK(!ep.m_ssl);
    WALLET_CHECK(ep.m_host == "localhost");
    WALLET_CHECK(ep.m_port == 8545);
    WALLET_CHECK(ep.m_pathAndQuery == "/");

    ep = parseOk("http://192.168.1.20:8545");
    WALLET_CHECK(ep.m_host == "192.168.1.20");
    WALLET_CHECK(ep.m_port == 8545);

    ep = parseOk("HTTPS://ETH-mainnet.g.alchemy.com/v2/MyKey_123-x");
    WALLET_CHECK(ep.m_ssl && ep.m_port == 443);
    WALLET_CHECK(ep.m_host == "eth-mainnet.g.alchemy.com"); // host lowercased
    WALLET_CHECK(ep.m_pathAndQuery == "/v2/MyKey_123-x");   // path case preserved

    ep = parseOk("  http://node.internal:8545/rpc?key=1  "); // outer whitespace trimmed
    WALLET_CHECK(ep.m_pathAndQuery == "/rpc?key=1");

    ep = parseOk("http://plainhost"); // default port from scheme
    WALLET_CHECK(ep.m_port == 80);
}

void TestRejects()
{
    checkReject("");
    checkReject("mainnet.infura.io/v3/abc");           // scheme-less
    // IPv6 literals: the transport is IPv4-only, the parser must not accept
    // what can never connect
    checkReject("http://[::1]:8545");
    checkReject("http://[2001:db8::1]:8545");
    checkReject("http://[::ffff:192.0.2.1]:8545");
    checkReject("ftp://host/path");
    checkReject("file:///etc/passwd");
    checkReject("ws://host:8546");
    checkReject("https://user:pass@host/path");        // embedded credentials
    checkReject("http://host:0");                      // port out of range
    checkReject("http://host:65536");
    checkReject("http://host:8545/pa th");             // inner whitespace
    checkReject(std::string("http://host/\x01path"));  // control char
    checkReject("http://");                            // empty host
    checkReject("http://host:notaport");
    checkReject("http://[not*valid$stuff]:8545");        // invalid bracket content
    checkReject("http://[::1");                          // unterminated bracket
    checkReject("http://[]:8545");                        // empty bracket content
    checkReject("http://[::1/foo]:8545");                 // '/' before closing bracket
}

void TestSanitize()
{
    WALLET_CHECK(SanitizeRpcUrlForLog("https://mainnet.infura.io/v3/SECRET") == "https://mainnet.infura.io");
    WALLET_CHECK(SanitizeRpcUrlForLog("http://localhost:8545/key") == "http://localhost:8545");
    WALLET_CHECK(SanitizeRpcUrlForLog("garbage") == "<invalid rpc url>");
}

void TestSettingsAccessors()
{
    Settings s;
    s.m_projectID = "abc";
    WALLET_CHECK(s.GetPathAndQuery() == "/v3/abc");
    WALLET_CHECK(s.NeedSsl());

    s.m_useCustomRpc = true;
    s.m_customRpcUrl = "http://localhost:8545/rpc";
    WALLET_CHECK(s.GetEthNodeAddress() == "localhost:8545");
    WALLET_CHECK(s.GetEthNodeHost() == "localhost");
    WALLET_CHECK(!s.NeedSsl());
    WALLET_CHECK(s.GetPathAndQuery() == "/rpc");

    Settings t = s;
    WALLET_CHECK(s == t);
    t.m_customRpcUrl = "http://other:8545";
    WALLET_CHECK(s != t); // endpoint change must trigger bridge reset
}

int main()
{
    beam::Rules r;
    beam::Rules::Scope scopeRules(r);

    std::cout << "Ethereum RPC endpoint parser tests:" << std::endl;
    TestAccepts();
    TestRejects();
    TestSanitize();
    TestSettingsAccessors();
    return WALLET_CHECK_RESULT;
}
