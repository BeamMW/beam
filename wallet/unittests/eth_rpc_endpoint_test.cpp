#include <iostream>
#include "test_helpers.h"
#include "wallet/transactions/swaps/bridges/ethereum/rpc_endpoint.h"
#include "wallet/transactions/swaps/bridges/ethereum/settings.h"
#include "wallet/transactions/swaps/bridges/ethereum/common.h"
#include "wallet/transactions/swaps/utils.h"

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

void TestPerTokenUnitsHelpers()
{
    // ETH/DAI: 18 decimals -> today's constants (UnitsPerCoin == 10^9, GetCoinUnitsMultiplier == 10^9)
    WALLET_CHECK(WalletUnitsPerToken(18) == 1'000'000'000ULL);
    WALLET_CHECK(TokenUnitsMultiplier(18) == 1'000'000'000u);

    // USDT: 6 decimals -> today's constants (UnitsPerCoin == 10^6, GetCoinUnitsMultiplier == 1)
    WALLET_CHECK(WalletUnitsPerToken(6) == 1'000'000ULL);
    WALLET_CHECK(TokenUnitsMultiplier(6) == 1u);

    // WBTC: 8 decimals -> today's constants (UnitsPerCoin == satoshi_per_bitcoin == 10^8, GetCoinUnitsMultiplier == 1)
    WALLET_CHECK(WalletUnitsPerToken(8) == 100'000'000ULL);
    WALLET_CHECK(TokenUnitsMultiplier(8) == 1u);

    // 0 decimals -> both sides collapse to the identity
    WALLET_CHECK(WalletUnitsPerToken(0) == 1ULL);
    WALLET_CHECK(TokenUnitsMultiplier(0) == 1u);

    // 9 decimals -> boundary: all precision fits the wallet Amount, no on-wire scaling needed
    WALLET_CHECK(WalletUnitsPerToken(9) == 1'000'000'000ULL);
    WALLET_CHECK(TokenUnitsMultiplier(9) == 1u);

    // kMaxTokenDecimals (18) -> boundary of the allowed range, still exact
    WALLET_CHECK(WalletUnitsPerToken(18) == 1'000'000'000ULL);
    WALLET_CHECK(TokenUnitsMultiplier(18) == 1'000'000'000u);

    // NOTE: decimals > kMaxTokenDecimals (e.g. 19, 255) is intentionally not exercised
    // here via a direct call: WalletUnitsPerToken/TokenUnitsMultiplier assert(decimals
    // <= kMaxTokenDecimals) before clamping, and this suite builds with assertions
    // enabled (Debug), so such a call aborts the process before the clamped value could
    // ever be observed - that's the intended fail-fast for a programmer error reaching
    // these helpers directly. The real defense against an attacker-controlled decimals
    // is upstream, at the two points that can observe a value coming from a
    // counterparty/peer: SwapOffersBoard::isExtendedOfferDataValid (board_test.cpp) and
    // EthereumBridge::getTokenInfo's decode. The clamp math itself (10^min(decimals,9)
    // and 10^max(0,decimals-9) evaluated at decimals=kMaxTokenDecimals) is covered by
    // the kMaxTokenDecimals boundary case above.
}

void TestIsLockTxAmountValidErc20()
{
    using namespace beam::wallet;

    // Erc20Token must route like the other ethereum-based coins (receiver pays
    // fee), not fall through to the "unsupported coin" default and throw.
    WALLET_CHECK(IsLockTxAmountValid(AtomicSwapCoin::Erc20Token, 1, 1));
    WALLET_CHECK(IsLockTxAmountValid(AtomicSwapCoin::Erc20Token, 0, 0));

    // A classic (non-ethereum-based) coin's result must be unchanged.
    WALLET_CHECK(!IsLockTxAmountValid(AtomicSwapCoin::Bitcoin, 1, 1));
}

namespace
{
std::string makeWord(const std::string& lowByteHex, const std::string& highBytesHex = std::string(62, '0'))
{
    return highBytesHex + lowByteHex;
}
}

void TestParseTokenDecimalsWord()
{
    uint8_t decimals = 0xFF;

    // Accepted values: proper 64-char words, only the low byte set.
    WALLET_CHECK(ParseTokenDecimalsWord(makeWord("00"), decimals) && decimals == 0);
    WALLET_CHECK(ParseTokenDecimalsWord(makeWord("06"), decimals) && decimals == 6);
    WALLET_CHECK(ParseTokenDecimalsWord(makeWord("08"), decimals) && decimals == 8);
    WALLET_CHECK(ParseTokenDecimalsWord(makeWord("09"), decimals) && decimals == 9);
    WALLET_CHECK(ParseTokenDecimalsWord(makeWord("12"), decimals) && decimals == 18);

    // Rejected: value beyond kMaxTokenDecimals (18).
    WALLET_CHECK(!ParseTokenDecimalsWord(makeWord("13"), decimals)); // 19
    WALLET_CHECK(!ParseTokenDecimalsWord(makeWord("ff"), decimals)); // 255

    // Rejected: a high byte is set.
    WALLET_CHECK(!ParseTokenDecimalsWord(makeWord("06", std::string(60, '0') + "01"), decimals));

    // Rejected: wrong length.
    WALLET_CHECK(!ParseTokenDecimalsWord("06", decimals));
    WALLET_CHECK(!ParseTokenDecimalsWord(makeWord("06") + "00", decimals));
    WALLET_CHECK(!ParseTokenDecimalsWord(std::string(), decimals));

    // Rejected: non-hex characters.
    WALLET_CHECK(!ParseTokenDecimalsWord(std::string(62, '0') + "zz", decimals));
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
    TestPerTokenUnitsHelpers();
    TestIsLockTxAmountValidErc20();
    TestParseTokenDecimalsWord();
    return WALLET_CHECK_RESULT;
}
