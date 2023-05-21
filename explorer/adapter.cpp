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

#include "adapter.h"
#include "node/node.h"
#include "core/serialization_adapters.h"
#include "bvm/bvm2.h"
#include "http/http_msg_creator.h"
#include "http/http_json_serializer.h"
#include "utility/helpers.h"
#include "utility/logger.h"

#include "wallet/core/common.h"
#include "wallet/core/common_utils.h"
#include "wallet/core/wallet.h"
#include "wallet/core/wallet_db.h"
#include "wallet/client/wallet_client.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/core/wallet_network.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include <boost/algorithm/string.hpp>
#include <boost/serialization/nvp.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include "wallet/client/extensions/offers_board/offers_protocol_handler.h"
#include "wallet/client/extensions/offers_board/swap_offers_board.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

namespace beam { namespace explorer {

namespace {

static const size_t PACKER_FRAGMENTS_SIZE = 4096;

const unsigned int FAKE_SEED = 10283UL;
const char WALLET_DB_PATH[] = "explorer-wallet.db";
const char WALLET_DB_PASS[] = "1";

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
std::string SwapAmountToString(Amount swapAmount, wallet::AtomicSwapCoin swapCoin)
{
    auto decimals = std::lround(std::log10(wallet::UnitsPerCoin(swapCoin)));
    boost::multiprecision::cpp_dec_float_50 preciseAmount(swapAmount);
    preciseAmount /= wallet::UnitsPerCoin(swapCoin);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(decimals) << preciseAmount;
    auto str = ss.str();

    const auto point = std::use_facet< std::numpunct<char>>(ss.getloc()).decimal_point();
    boost::algorithm::trim_right_if(str, boost::is_any_of("0"));
    boost::algorithm::trim_right_if(str, [point](const char ch) {return ch == point; });

    return str;
}
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

const char* hash_to_hex(char* buf, const Merkle::Hash& hash) {
    return to_hex(buf, hash.m_pData, hash.nBytes);
}

const char* uint256_to_hex(char* buf, const ECC::uintBig& n) {
    char* p = to_hex(buf + 2, n.m_pData, n.nBytes);
    while (p && *p == '0') ++p;
    if (*p == '\0') --p;
    *--p = 'x';
    *--p = '0';
    return p;
}

const char* difficulty_to_hex(char* buf, const Difficulty& d) {
    ECC::uintBig raw;
    d.Unpack(raw);
    return uint256_to_hex(buf, raw);
}



} //namespace

class ExchangeRateProvider
        : public IBroadcastListener
{
public:
    ExchangeRateProvider(IBroadcastMsgGateway& broadcastGateway, const wallet::IWalletDB::Ptr& walletDB) :
        _broadcastGateway(broadcastGateway),
        _walletDB(walletDB)
    {
        PeerID key;
        if (wallet::BroadcastMsgValidator::stringToPublicKey(wallet::get_BroadcastValidatorPublicKey(), key))
        {
            _validator.setPublisherKeys( { key } );
        }
        _broadcastGateway.registerListener(BroadcastContentType::ExchangeRates, this);
    }

    virtual ~ExchangeRateProvider() = default;

    std::string getBeamTo(const wallet::Currency& toCurrency, uint64_t height)
    {
        if (height >= _preloadStartHeight && height <= _preloadEndHeight)
        {
            auto it = std::find_if(
                _ratesCache.begin(), _ratesCache.end(),
                [toCurrency, height] (const wallet::ExchangeRateAtPoint& rate) {
                    return rate.m_from == wallet::Currency::BEAM()
                        && rate.m_to == toCurrency
                        && rate.m_height <= height;
                });
            return it != _ratesCache.end()
                ? std::to_string(wallet::PrintableAmount(it->m_rate, true))
                : "-";
        }
        else
        {
            const auto rate = _walletDB->getExchangeRateNearPoint(wallet::Currency::BEAM(), toCurrency, height);
            return rate ? std::to_string(wallet::PrintableAmount(rate->m_rate, true)) : "-";
        }
    }

    void preloadRates(uint64_t startHeight, uint64_t endHeight)
    {
        if (endHeight < startHeight) return;
        _preloadStartHeight = startHeight;
        _preloadEndHeight = endHeight;

        const auto btcFirstRate = _walletDB->getExchangeRateNearPoint(
            wallet::Currency::BEAM(),
            wallet::Currency::BTC(),
            startHeight);

        const auto usdFirstRate = _walletDB->getExchangeRateNearPoint(
            wallet::Currency::BEAM(),
            wallet::Currency::BTC(),
            startHeight);

        auto minHeight = std::min(btcFirstRate ? btcFirstRate->m_height : 0, usdFirstRate ? usdFirstRate->m_height : 0);
        _ratesCache = _walletDB->getExchangeRatesHistory(minHeight, endHeight);
    }

    // IBroadcastListener implementation
    bool onMessage(BroadcastMsg&& msg) override
    {
        Block::SystemState::Full blockState;
        _walletDB->get_History().get_Tip(blockState);
        if (!blockState.m_Height) return false;

        if (_validator.isSignatureValid(msg))
        {
            try
            {
                std::vector<wallet::ExchangeRate> rates;
                if (wallet::fromByteBuffer(msg.m_content, rates))
                {
                    for (auto& rate : rates)
                    {
                        wallet::ExchangeRateAtPoint rateHistory(rate, blockState.m_Height);
                        _walletDB->saveExchangeRate(rateHistory);
                    }
                }
            }
            catch(...)
            {
                LOG_WARNING() << "broadcast message processing exception";
                return false;
            }
        }
        return true;
    }
    
private:
    IBroadcastMsgGateway& _broadcastGateway;
    wallet::IWalletDB::Ptr _walletDB;
    wallet::BroadcastMsgValidator _validator;
    uint64_t _preloadStartHeight = 0;
    uint64_t _preloadEndHeight = 0;
    wallet::ExchangeRatesHistory _ratesCache;
};

/// Explorer server backend, gets callback on status update and returns json messages for server
class Adapter : public Node::IObserver, public IAdapter {
public:
    Adapter(Node& node) :
        _packer(PACKER_FRAGMENTS_SIZE),
		_node(node),
        _nodeBackend(node.get_Processor()),
        _nodeIsSyncing(true)
    {
         init_helper_fragments();
         _hook = &node.m_Cfg.m_Observer;
         _nextHook = *_hook;
         *_hook = this;

         if (!wallet::WalletDB::isInitialized(WALLET_DB_PATH))
         {
            ECC::NoLeak<ECC::uintBig> seed;
            seed.V = FAKE_SEED;
            _walletDB = wallet::WalletDB::init(WALLET_DB_PATH, SecString(WALLET_DB_PASS), seed, false);
         }
         else
         {
             _walletDB = wallet::WalletDB::open(WALLET_DB_PATH, SecString(WALLET_DB_PASS));
         }
         
        _wallet = std::make_shared<wallet::Wallet>(_walletDB);
        auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(*_wallet);
        nnet->m_Cfg.m_vNodes.push_back(node.m_Cfg.m_Listen);
        nnet->Connect();

        auto wnet = std::make_shared<wallet::WalletNetworkViaBbs>(*_wallet, nnet, _walletDB);

        _wallet->AddMessageEndpoint(wnet);
        _wallet->SetNodeEndpoint(nnet);

        _broadcastRouter = std::make_shared<BroadcastRouter>(nnet, *wnet, std::make_shared<BroadcastRouter::BbsTsHolder>(_walletDB));
        _exchangeRateProvider = std::make_shared<ExchangeRateProvider>(*_broadcastRouter, _walletDB);

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        _offerBoardProtocolHandler =
            std::make_shared<wallet::OfferBoardProtocolHandler>(_walletDB->get_SbbsKdf());
        _offersBulletinBoard = std::make_shared<wallet::SwapOffersBoard>(
            *_broadcastRouter, *_offerBoardProtocolHandler, _walletDB);
        _walletDbSubscriber = std::make_unique<WalletDbSubscriber>(
                    static_cast<wallet::IWalletDbObserver*>(_offersBulletinBoard.get()), _walletDB);
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
    }

    virtual ~Adapter() {
        if (_nextHook) *_hook = _nextHook;
    }

private:
    void init_helper_fragments() {
        static const char* s = "[,]\"";
        io::SharedBuffer buf(s, 4);
        _leftBrace = buf;
        _leftBrace.size = 1;
        _comma = buf;
        _comma.size = 1;
        _comma.data ++;
        _rightBrace = buf;
        _rightBrace.size = 1;
        _rightBrace.data += 2;
        _quote = buf;
        _quote.size = 1;
        _quote.data += 3;
    }

    /// Returns body for /status request
    void OnSyncProgress() override {
		const Node::SyncStatus& s = _node.m_SyncStatus;
        bool isSyncing = (s.m_Done != s.m_Total);
        _nodeIsSyncing = isSyncing;
        if (_nextHook) _nextHook->OnSyncProgress();
    }

    void OnStateChanged() override {
        if (_nextHook) _nextHook->OnStateChanged();
    }

    void OnRolledBack(const Block::SystemState::ID& id) override {
        if (_nextHook) _nextHook->OnRolledBack(id);
    }

    json get_status() override {

        Height h = _nodeBackend.m_Cursor.m_Full.m_Height;

        double possibleShieldedReadyHours = 0;
        uint64_t shieldedPer24h = 0;

        if (h)
        {
            NodeDB& db = _nodeBackend.get_DB();
            auto shieldedByLast24h = db.ShieldedOutpGet(h >= 1440 ? h - 1440 : 1);
            auto averageWindowBacklog = Rules::get().Shielded.MaxWindowBacklog / 2;

            if (shieldedByLast24h && shieldedByLast24h != _nodeBackend.m_Extra.m_ShieldedOutputs)
            {
                shieldedPer24h = _nodeBackend.m_Extra.m_ShieldedOutputs - shieldedByLast24h;
                possibleShieldedReadyHours = ceil(averageWindowBacklog / (double)shieldedPer24h * 24);
            }
        }

        char buf[80];

        json j{
                { "timestamp", _nodeBackend.m_Cursor.m_Full.m_TimeStamp },
                { "height", h },
                { "low_horizon", _nodeBackend.m_Extra.m_TxoHi },
                { "hash", hash_to_hex(buf, _nodeBackend.m_Cursor.m_ID.m_Hash) },
                { "chainwork",  uint256_to_hex(buf, _nodeBackend.m_Cursor.m_Full.m_ChainWork) },
                { "peers_count", _node.get_AcessiblePeerCount() },
                { "shielded_outputs_total", _nodeBackend.m_Extra.m_ShieldedOutputs },
                { "shielded_outputs_per_24h", shieldedPer24h },
                { "shielded_possible_ready_in_hours", shieldedPer24h ? std::to_string(possibleShieldedReadyHours) : "-" }
        };

        return j;
    }

    bool extract_row(Height height, uint64_t& row, uint64_t* prevRow) {
        NodeDB& db = _nodeBackend.get_DB();
        NodeDB::WalkerState ws;
        db.EnumStatesAt(ws, height);
        while (true) {
            if (!ws.MoveNext()) {
                return false;
            }
            if (NodeDB::StateFlags::Active & db.GetStateFlags(ws.m_Sid.m_Row)) {
                row = ws.m_Sid.m_Row;
                break;
            }
        }
        if (prevRow) {
            *prevRow = row;
            if (!db.get_Prev(*prevRow)) {
                *prevRow = 0;
            }
        }
        return true;
    }

    template <typename T>
    static void AssignField(json& json, const char* szName, const T& val)
    {
        if (szName)
            json[szName] = val;
        else
            json.push_back(val);
    }

    static void AssignField(json& x, const char* szName, json&& y)
    {
        if (szName)
            x[szName] = std::move(y);
        else
            x.push_back(std::move(y));
    }

    template <uint32_t nBytes>
    static void AssignField(json& json, const char* szName, const uintBig_t<nBytes>& val)
    {
        char sz[uintBig_t<nBytes>::nTxtLen + 1];
        val.Print(sz);
        AssignField(json, szName, std::move(sz));
    }

    static void AssignField(json& json, const char* szName, const ECC::Point& pt)
    {
        typedef uintBig_t<ECC::nBytes + 1> MyPoint;
        AssignField<MyPoint::nBytes>(json, szName, std::move(Cast::Reinterpret<MyPoint>(pt)));
    }

    static void AssignField(json& json, const char* szName, const PeerID& pid)
    {
        AssignField(json, szName, (const ECC::uintBig&) pid);
    }

    template <typename T>
    static json MakeTypeObj(const char* szType, const T& val)
    {
        json j;
        j["type"] = szType;
        AssignField(j, "value", val);
        return j;
    }

    static json MakeTableHdr(const char* szName)
    {
        return MakeTypeObj("th", szName);
    }

    static json MakeTable(json&& jRows)
    {
        return MakeTypeObj("table", std::move(jRows));
    }

    static json MakeObjAid(Asset::ID aid)
    {
        return MakeTypeObj("aid", aid);
    }

    static json MakeObjAmount(const Amount x)
    {
        return MakeTypeObj("amount", x);
    }

    static json MakeObjHeight(Height h)
    {
        return MakeTypeObj("height", h);
    }

    static json MakeObjCid(const ContractID& cid)
    {
        return MakeTypeObj("cid", cid);
    }

    static json MakeObjBool(bool val)
    {
        return MakeTypeObj("bool", val ? 1u : 0u);
    }

    template <typename T>
    static json MakeObjBlob(const T& x)
    {
        return MakeTypeObj("blob", x);
    }

    static json MakeObjAmount(const AmountBig::Type& x)
    {
        if (!AmountBig::get_Hi(x))
            return MakeObjAmount(AmountBig::get_Lo(x));

        char sz[AmountBig::Type::nTxtLen10Max + 2];

        if (x.get_Msb())
        {
            auto x2 = x;
            x2.Negate();

            sz[0] = '-';
            x2.PrintDecimal(sz + 1);
        }
        else
            x.PrintDecimal(sz);

        return MakeTypeObj("amount", sz);
    }

    struct ExtraInfo
    {
        struct ContractRichInfo {
            std::vector<NodeProcessor::ContractInvokeExtraInfo> m_vInfo;
            size_t m_iPos = 0;

            const NodeProcessor::ContractInvokeExtraInfo* get_Next()
            {
                if (m_iPos >= m_vInfo.size())
                    return nullptr;

                auto& ret = m_vInfo[m_iPos];
                m_iPos += (ret.m_NumNested + 1);

                return &ret;
            }
        };

        static void FundsToExclusive(NodeProcessor::ContractInvokeExtraInfo& info)
        {
            for (uint32_t iNested = 0; iNested < info.m_NumNested; )
            {
                auto& infoNested = (&info)[++iNested];
                iNested += infoNested.m_NumNested;

                FundsToExclusive(infoNested);

                for (auto it = infoNested.m_FundsIO.m_Map.begin(); infoNested.m_FundsIO.m_Map.end() != it; it++)
                {
                    auto val = it->second;
                    val.Negate();
                    info.m_FundsIO.Add(val, it->first);
                }
            }
        }

        struct Writer
        {
            json m_json;

            template <uint32_t nBytes>
            void AddHex(const char* szName, const uintBig_t<nBytes>& val)
            {
                AssignField(m_json, szName, val);
            }

            void AddCommitment(const ECC::Point& pt)
            {
                AssignField(m_json, "commitment", pt);
            }

            template <typename T>
            void AddMinMax(const T& vMin, const T& vMax)
            {
                m_json["min"] = vMin;
                m_json["max"] = vMax;
            }

            void AddAid(Asset::ID aid)
            {
                m_json["aid"] = aid;
            }

            void AddValBig(const char* szName, const AmountBig::Type& x)
            {
                if (!AmountBig::get_Hi(x))
                {
                    auto valLo = AmountBig::get_Lo(x);
                    if (szName)
                        m_json[szName] = valLo;
                    else
                        m_json.push_back(valLo);
                }
                else
                {
                    char sz[AmountBig::Type::nTxtLen10Max + 1];
                    x.PrintDecimal(sz);

                    if (szName)
                        m_json[szName] = sz;
                    else
                        m_json.push_back(sz);
                }
            }

            Writer()
            {
                m_json = json::object();
            }

            Writer(json&& x)
            {
                m_json = std::move(x);
            }

            void OnAsset(const Asset::Proof* pProof)
            {
                if (pProof)
                {
                    auto t0 = pProof->m_Begin;

                    Writer wr;
                    wr.AddMinMax(t0, t0 + Rules::get().CA.m_ProofCfg.get_N() - 1);
                    m_json["Asset"] = std::move(wr.m_json);
                }
            }

            void OnContract(const NodeProcessor::ContractInvokeExtraInfo& info)
            {
                Writer wrArr(json::array());
                wrArr.m_json.push_back(json::array({
                    MakeTableHdr("Cid"),
                    MakeTableHdr("Kind"),
                    MakeTableHdr("Method"),
                    MakeTableHdr("Arguments"),
                    MakeTableHdr("Funds"),
                    MakeTableHdr("Keys")
                    }));

                
                wrArr.OnContractInternal(info, nullptr, 0, 0);

                m_json["Contract"] = MakeTable(std::move(wrArr.m_json));
            }

            void ParseSafe(const std::string& s)
            {
                if (!s.empty())
                {
                    m_json = json::parse(s, nullptr, false); // won't throw exc

                    if (!m_json.is_object())
                        m_json = json::object();
                }
            }

            void OnContractInternalRaw(const NodeProcessor::ContractInvokeExtraInfo& info, const bvm2::ContractID* pDefCid, uint32_t nIndent, Height h)
            {
                Writer wrSrc;
                wrSrc.ParseSafe(info.m_sParsed);

                if (pDefCid)
                {
                    // Height
                    if (nIndent)
                        m_json.push_back("");
                    else
                        m_json.push_back(h);
                }

                // Cid, Kind
                if (pDefCid && (info.m_Cid == *pDefCid))
                {
                    m_json.push_back("");
                    m_json.push_back("");
                }
                else
                {
                    m_json.push_back(MakeObjCid(info.m_Cid));

                    auto it = wrSrc.m_json.find("kind");
                    if (wrSrc.m_json.end() == it)
                    {
                        if (info.m_Sid)
                            AddHex(nullptr, *info.m_Sid);
                        else
                            m_json.push_back("");
                    }
                    else
                        m_json.push_back(std::move(*it));
                }

                // Method, Arguments
                {
                    auto it = wrSrc.m_json.find("method");
                    if (wrSrc.m_json.end() == it)
                    {
                        // as blob
                        switch (info.m_iMethod)
                        {
                        case 0: m_json.push_back("Create"); break;
                        case 1: m_json.push_back("Destroy"); break;
                        default: m_json.push_back(info.m_iMethod);
                        }

                        std::string sArgs;
                        if (!info.m_Args.empty())
                        {
                            sArgs.resize(info.m_Args.size() * 2);
                            uintBigImpl::_Print(&info.m_Args.front(), (uint32_t) info.m_Args.size(), &sArgs.front());
                        }

                        m_json.push_back(std::move(sArgs));
                    }
                    else
                    {
                        m_json.push_back(std::move(*it));

                        it = wrSrc.m_json.find("params");
                        if (wrSrc.m_json.end() == it)
                            m_json.push_back("");
                        else
                            m_json.push_back(std::move(*it));
                    }
                }

                // Funds.
                if (!info.m_iParent)
                    FundsToExclusive(Cast::NotConst(info));

                if (!info.m_FundsIO.m_Map.empty())
                {
                    json jArr = json::array();

                    for (auto it = info.m_FundsIO.m_Map.begin(); info.m_FundsIO.m_Map.end() != it; it++)
                    {
                        auto val = it->second;
                        val.Negate();

                        json jEntry = json::array();
                        jEntry.push_back(MakeObjAid(it->first));
                        jEntry.push_back(MakeObjAmount(val));

                        jArr.push_back(std::move(jEntry));
                    }

                    m_json.push_back(MakeTable(std::move(jArr)));
                }
                else
                    m_json.push_back("");


                // Keys
                if (!info.m_vSigs.empty())
                {
                    json jArr = json::array();

                    for (uint32_t iSig = 0; iSig < info.m_vSigs.size(); iSig++)
                    {
                        json jEntry = json::array();
                        AssignField(jEntry, nullptr, MakeObjBlob(info.m_vSigs[iSig]));
                        jArr.push_back(std::move(jEntry));
                    }

                    m_json.push_back(MakeTable(std::move(jArr)));
                }
                else
                    m_json.push_back("");

                // debug info
                {
                    auto it = wrSrc.m_json.find("dbg");
                    if (wrSrc.m_json.end() != it)
                        m_json.push_back(std::move(*it));
                }
            }

            void OnContractInternal(const NodeProcessor::ContractInvokeExtraInfo& info, const bvm2::ContractID* pDefCid, uint32_t nIndent, Height h)
            {
                Writer wr(json::array());
                wr.OnContractInternalRaw(info, pDefCid, nIndent, h);

                if (info.m_NumNested)
                {
                    Writer wrGroup(json::array());
                    wrGroup.m_json.push_back(std::move(wr.m_json));

                    for (uint32_t iNested = 0; iNested < info.m_NumNested; )
                    {
                        const auto& infoNested = (&info)[++iNested];
                        iNested += infoNested.m_NumNested;

                        wrGroup.OnContractInternal(infoNested, pDefCid, nIndent + 1, h);
                    }

                    m_json.push_back(MakeTypeObj("group", std::move(wrGroup.m_json)));
                }
                else
                    m_json.push_back(std::move(wr.m_json));

            }

            void AddMetadata(const Asset::Metadata& md)
            {
                std::string s;
                md.get_String(s);

                Writer wr;
                wr.m_json["text"] = std::move(s);
                wr.m_json["hash"] = MakeObjBlob(md.m_Hash);

                m_json["metadata"] = std::move(wr.m_json);
            }

            static json get_AssetOwner(const Asset::CreateInfo& ai)
            {
                return (ai.m_Cid != Zero) ? 
                    MakeObjCid(ai.m_Cid) :
                    MakeObjBlob(ai.m_Owner);
            }

            void AddAssetCreateInfo(const Asset::CreateInfo& ai)
            {
                AddMetadata(ai.m_Metadata);
                m_json["owner"] = get_AssetOwner(ai);
                m_json["deposit"] = MakeObjAmount(ai.m_Deposit);
            }

            void AddAssetInfo(const Asset::Full& ai)
            {
                AddAid(ai.m_ID);
                AddAssetCreateInfo(ai);
                AddValBig("value", ai.m_Value);
                m_json["lock_height"] = ai.m_LockHeight;
            }
        };

        static json get(const Output& outp, Height h, Height hMaturity, Mode m)
        {
            Writer w;
            w.AddCommitment(outp.m_Commitment);

            if (outp.m_Coinbase)
                w.m_json["type"] = "Coinbase";

            if (outp.m_pPublic)
            {
                if (Mode::Legacy == m)
                    w.m_json["Value"] = outp.m_pPublic->m_Value;
                else
                    w.m_json["Value"] = MakeObjAmount(outp.m_pPublic->m_Value);
            }

            if (outp.m_Incubation)
                w.m_json["Incubation"] = outp.m_Incubation;

            if (hMaturity != h)
            {
                if (Mode::Legacy == m)
                    w.m_json["Maturity"] = hMaturity;
                else
                    w.m_json["Maturity"] = MakeObjHeight(hMaturity);
            }

            w.OnAsset(outp.m_pAsset.get());

            return std::move(w.m_json);
        }

        static json get(const TxKernel& krn, Amount& fee, ContractRichInfo& cri, Height h, Mode m)
        {
            struct MyWalker
            {
                Writer m_Wr;
                ContractRichInfo* m_pCri;
                Height m_Height;
                Mode m_Mode;

                void OnKrn(const TxKernel& krn)
                {
                    switch (krn.get_Subtype())
                    {
#define THE_MACRO(id, name) case id: OnKrnEx(Cast::Up<TxKernel##name>(krn)); break;
                        BeamKernelsAll(THE_MACRO)
#undef THE_MACRO
                    }
                }

                void OnKrnEx(const TxKernelStd& krn)
                {
                    if (krn.m_pRelativeLock)
                    {
                        Writer wr;
                        wr.AddHex("ID", krn.m_pRelativeLock->m_ID);
                        wr.m_json["height"] = krn.m_pRelativeLock->m_LockHeight;
                        m_Wr.m_json["Rel.Lock"] = std::move(wr.m_json);
                    }

                    if (krn.m_pHashLock)
                    {
                        Writer wr;
                        wr.AddHex("Preimage", krn.m_pHashLock->m_Value);
                        m_Wr.m_json["Hash.Lock"] = std::move(wr.m_json);
                    }
                }

                void OnKrnEx(const TxKernelAssetCreate& krn)
                {
                    Asset::CreateInfo ai;
                    ai.SetCid(nullptr);
                    ai.m_Owner = krn.m_Owner;
                    ai.m_Deposit = Rules::get().get_DepositForCA(m_Height);

                    TemporarySwap ts(ai.m_Metadata, Cast::NotConst(krn).m_MetaData);

                    Writer wr;
                    wr.AddAssetCreateInfo(ai);
                    m_Wr.m_json["Asset.Create"] = std::move(wr.m_json);
                }

                void OnKrnEx(const TxKernelAssetDestroy& krn)
                {
                    Writer wr;
                    wr.AddAid(krn.m_AssetID);
                    wr.m_json["Deposit"] = krn.get_Deposit();
                    m_Wr.m_json["Asset.Destroy"] = std::move(wr.m_json);
                }

                void OnKrnEx(const TxKernelAssetEmit& krn)
                {
                    Writer wr;
                    wr.AddAid(krn.m_AssetID);
                    wr.m_json["Value"] = krn.m_Value;
                    m_Wr.m_json["Asset.Emit"] = std::move(wr.m_json);
                }

                void OnKrnEx(const TxKernelShieldedOutput& krn)
                {
                    Writer wr2;
                    wr2.OnAsset(krn.m_Txo.m_pAsset.get());

                    m_Wr.m_json["Shielded.Out"] = std::move(wr2.m_json);
                }

                void OnKrnEx(const TxKernelShieldedInput& krn)
                {
                    Writer wr2;
                    wr2.OnAsset(krn.m_pAsset.get());

                    uint32_t n = krn.m_SpendProof.m_Cfg.get_N();

                    TxoID id0 = krn.m_WindowEnd;
                    if (id0 > n)
                        id0 -= n;
                    else
                        id0 = 0;

                    wr2.AddMinMax(id0, krn.m_WindowEnd - 1);

                    m_Wr.m_json["Shielded.In"] = std::move(wr2.m_json);
                }

                void OnContract(const TxKernelContractControl& krn, const NodeProcessor::ContractInvokeExtraInfo& cri)
                {
                    m_Wr.m_json["HFTX"] = MakeObjBool(krn.m_Dependent);
                    m_Wr.OnContract(cri);
                }

                void OnKrnEx(const TxKernelContractCreate& krn)
                {
                    auto pInfo = m_pCri->get_Next();
                    if (pInfo)
                        OnContract(krn, *pInfo);
                    else
                    {
                        NodeProcessor::ContractInvokeExtraInfo info;
                        info.m_NumNested = 0;
                        info.m_iParent = 0;

                        bvm2::ShaderID sid;
                        bvm2::get_ShaderID(sid, krn.m_Data);
                        bvm2::get_CidViaSid(info.m_Cid, sid, krn.m_Args);

                        info.SetUnk(0, krn.m_Args, &sid);

                        OnContract(krn, info);
                    }
                }

                void OnKrnEx(const TxKernelContractInvoke& krn)
                {
                    auto pInfo = m_pCri->get_Next();
                    if (pInfo)
                        OnContract(krn, *pInfo);
                    else
                    {
                        NodeProcessor::ContractInvokeExtraInfo info;
                        info.m_Cid = krn.m_Cid;
                        info.SetUnk(krn.m_iMethod, krn.m_Args, nullptr);
                        OnContract(krn, info);
                    }
                }

            } wlk;
            wlk.m_pCri = &cri;
            wlk.m_Height = h;
            wlk.m_Mode = m;

            wlk.m_Wr.AddHex("id", krn.m_Internal.m_ID);

            auto hr = krn.get_EffectiveHeightRange();

            if (hr.m_Min)
                wlk.m_Wr.m_json["minHeight"] = hr.m_Min;
            if (hr.m_Max != MaxHeight)
                wlk.m_Wr.m_json["maxHeight"] = hr.m_Max;

            fee += krn.m_Fee;

            wlk.OnKrn(krn);

            if (!krn.m_vNested.empty())
            {
                json j2 = json::array();

                for (uint32_t i = 0; i < krn.m_vNested.size(); i++)
                {
                    json j3 = get(*krn.m_vNested[i], fee, cri, h, m);
                    j2.push_back(std::move(j3));
                }

                wlk.m_Wr.m_json["nested"] = std::move(j2);
            }

            return std::move(wlk.m_Wr.m_json);
        }
    };

    void get_LockedFunds(json& jArr, const bvm2::ContractID& cid)
    {
#pragma pack (push, 1)
        struct KeyFund
        {
            bvm2::ContractID m_Cid;
            uint8_t m_Tag = Shaders::KeyTag::LockedAmount;
            uintBigFor<Asset::ID>::Type m_Aid;
        };
#pragma pack (pop)

        KeyFund k0, k1;
        k0.m_Cid = cid;
        k0.m_Aid = Zero;
        k1.m_Cid = cid;
        k1.m_Aid = static_cast<Asset::ID>(-1);

        NodeDB::WalkerContractData wlk;
        for (_nodeBackend.get_DB().ContractDataEnum(wlk, Blob(&k0, sizeof(k0)), Blob(&k1, sizeof(k1))); wlk.MoveNext(); )
        {
            if ((sizeof(KeyFund) != wlk.m_Key.n) || (sizeof(AmountBig::Type) != wlk.m_Val.n))
                continue;

            Asset::ID aid;
            reinterpret_cast<const KeyFund*>(wlk.m_Key.p)->m_Aid.Export(aid);

            json jEntry = json::array();
            jEntry.push_back(MakeObjAid(aid));
            jEntry.push_back(MakeObjAmount(*reinterpret_cast<const AmountBig::Type*>(wlk.m_Val.p)));

            jArr.push_back(std::move(jEntry));
        }
    }

    void get_OwnedAssets(json& jArr, const bvm2::ContractID& cid)
    {
#pragma pack (push, 1)
        struct KeyAsset
        {
            bvm2::ContractID m_Cid;
            uint8_t m_Tag = Shaders::KeyTag::OwnedAsset;
            uintBigFor<Asset::ID>::Type m_Aid;
        };
#pragma pack (pop)

        KeyAsset k0, k1;
        k0.m_Cid = cid;
        k0.m_Aid = Zero;
        k1.m_Cid = cid;
        k1.m_Aid = static_cast<Asset::ID>(-1);

        NodeDB::WalkerContractData wlk;
        for (_nodeBackend.get_DB().ContractDataEnum(wlk, Blob(&k0, sizeof(k0)), Blob(&k1, sizeof(k1))); wlk.MoveNext(); )
        {
            if (sizeof(KeyAsset) != wlk.m_Key.n)
                continue;

            Asset::Full ai;
            reinterpret_cast<const KeyAsset*>(wlk.m_Key.p)->m_Aid.Export(ai.m_ID);

            if (!_nodeBackend.get_DB().AssetGetSafe(ai))
                ai.m_Value = Zero;

            std::string sMeta;
            ai.m_Metadata.get_String(sMeta);

            json jEntry = json::array();
            jEntry.push_back(MakeObjAid(ai.m_ID));
            jEntry.push_back(std::move(sMeta));
            jEntry.push_back(MakeObjAmount(ai.m_Value));

            jArr.push_back(std::move(jEntry));
        }
    }

    void get_ContractDescr(ExtraInfo::Writer& wr, const bvm2::ShaderID& sid, const bvm2::ContractID& cid, bool bFullState)
    {
        std::string sExtra;
        _nodeBackend.get_ContractDescr(sid, cid, sExtra, bFullState);

        wr.ParseSafe(sExtra);

        if (wr.m_json.find("kind") == wr.m_json.end())
            AssignField(wr.m_json, "kind", MakeObjBlob(sid));

    }

    json get_contracts() override
    {
        json j;

#pragma pack (push, 1)
        struct KeyEntry
        {
            bvm2::ContractID m_Zero;
            uint8_t m_Tag;

            struct SidCid
            {
                bvm2::ShaderID m_Sid;
                bvm2::ContractID m_Cid;
            } m_SidCid;
        };
#pragma pack (pop)

        KeyEntry k0, k1;
        ZeroObject(k0);
        k0.m_Tag = Shaders::KeyTag::SidCid;

        k1.m_Zero = Zero;
        k1.m_Tag = Shaders::KeyTag::SidCid;
        memset(reinterpret_cast<void*>(&k1.m_SidCid), 0xff, sizeof(k1.m_SidCid));

        std::vector<std::pair<KeyEntry::SidCid, Height> > vIDs;

        {
            NodeDB::WalkerContractData wlk;
            for (_nodeBackend.get_DB().ContractDataEnum(wlk, Blob(&k0, sizeof(k0)), Blob(&k1, sizeof(k1))); wlk.MoveNext(); )
            {
                if ((sizeof(KeyEntry) != wlk.m_Key.n) || (sizeof(Height) != wlk.m_Val.n))
                    continue;

                auto& x = vIDs.emplace_back();
                x.first = reinterpret_cast<const KeyEntry*>(wlk.m_Key.p)->m_SidCid;
                (reinterpret_cast<const uintBigFor<Height>::Type*>(wlk.m_Val.p))->Export(x.second);
            }
        }

        json out = json::array();
        out.push_back(json::array({
            MakeTableHdr("Cid"),
            MakeTableHdr("Kind"),
            MakeTableHdr("Deploy Height"),
            MakeTableHdr("Locked Funds"),
            MakeTableHdr("Owned Assets"),
        }));

        for (const auto& x : vIDs)
        {
            json jItem = json::array();
            jItem.push_back(MakeObjCid(x.first.m_Cid));

            {
                ExtraInfo::Writer wr;
                get_ContractDescr(wr, x.first.m_Sid, x.first.m_Cid, false);

                auto it = wr.m_json.find("kind");
                assert(wr.m_json.end() != it);
                jItem.push_back(std::move(*it));
            }

            jItem.push_back(x.second);

            {
                json jArr = json::array();
                get_LockedFunds(jArr, x.first.m_Cid);
                if (jArr.empty())
                    jItem.push_back("");
                else
                    jItem.push_back(MakeTable(std::move(jArr)));
            }

            {
                json jArr = json::array();
                get_OwnedAssets(jArr, x.first.m_Cid);
                if (jArr.empty())
                    jItem.push_back("");
                else
                    jItem.push_back(MakeTable(std::move(jArr)));
            }

            out.push_back(std::move(jItem));
        }

        return MakeTable(std::move(out));
    }

    static void OnKrnInfoCorrupted()
    {
        CorruptionException exc;
        exc.m_sErr = "KrnInfo";
        throw exc;
    }

    void get_ContractState(json& out, const bvm2::ContractID& cid, const HeightRange& hr, uint32_t nMaxTxs)
    {
        bool bExists = false;

        ExtraInfo::Writer wr;

        {
            Blob blob;
            NodeDB::Recordset rs;
            if (_nodeBackend.get_DB().ContractDataFind(cid, blob, rs))
            {
                bExists = true;
                bvm2::ShaderID sid;
                bvm2::get_ShaderID(sid, blob);
                get_ContractDescr(wr, sid, cid, true);
            }

        }

        if (bExists)
        {
            json jArr = json::array();
            jArr.push_back(json::array({
                MakeTableHdr("Asset ID"),
                MakeTableHdr("Amount")
                }));

            get_LockedFunds(jArr, cid);

            wr.m_json["Locked Funds"] = MakeTable(std::move(jArr));
        }

        if (bExists)
        {
            json jArr = json::array();

            jArr.push_back(json::array({
                MakeTableHdr("Asset ID"),
                MakeTableHdr("Metadata"),
                MakeTableHdr("Emission")
            }));

            get_OwnedAssets(jArr, cid);

            wr.m_json["Owned assets"] = MakeTable(std::move(jArr));
        }

        {
            json jArr = json::array();
            jArr.push_back(json::array({
                MakeTableHdr("Height"),
                MakeTableHdr("Version")
                }));

#pragma pack (push, 1)
            struct KeyVer
            {
                bvm2::ContractID m_Cid;
                uint8_t m_Tag = Shaders::KeyTag::ShaderChange;
            };
#pragma pack (pop)

            KeyVer key;
            key.m_Cid = cid;

            NodeDB::ContractLog::Walker wlk;
            for (_nodeBackend.get_DB().ContractLogEnum(wlk, Blob(&key, sizeof(key)), Blob(&key, sizeof(key)), HeightPos(0), HeightPos(MaxHeight)); wlk.MoveNext(); )
            {
                std::string sName;

                json jEntry = json::array();
                jEntry.push_back(MakeObjHeight(wlk.m_Entry.m_Pos.m_Height));

                if (sizeof(bvm2::ShaderID) == wlk.m_Entry.m_Val.n)
                {
                    const auto& sid = *(const bvm2::ShaderID*)wlk.m_Entry.m_Val.p;

                    ExtraInfo::Writer wr2;
                    get_ContractDescr(wr2, sid, cid, false); // assume it's safe, parser won't enumerate logs.

                    auto it = wr2.m_json.find("kind");
                    if (wr2.m_json.end() != it)
                        jEntry.push_back(std::move(*it));
                }
                else
                    jEntry.push_back("End of life");

                jArr.push_back(std::move(jEntry));
            }

            wr.m_json["Version History"] = MakeTable(std::move(jArr));
        }

        {
            ExtraInfo::Writer wrArr(json::array());
            wrArr.m_json.push_back(json::array({
                MakeTableHdr("Height"),
                MakeTableHdr("Cid"),
                MakeTableHdr("Kind"),
                MakeTableHdr("Method"),
                MakeTableHdr("Arguments"),
                MakeTableHdr("Funds"),
                MakeTableHdr("Keys")
                }));

            std::vector<NodeProcessor::ContractInvokeExtraInfo> vInfo;

            uint32_t nCount = 0;
            HeightPos hpLast = { 0 };
            bool bMore = false;

            std::setmin(nMaxTxs, 2000u);

            NodeDB::KrnInfo::Walker wlk;
            for (_nodeBackend.get_DB().KrnInfoEnum(wlk, cid, hr.m_Max); wlk.MoveNext(); nCount++)
            {
                if (hpLast.m_Height != wlk.m_Entry.m_Pos.m_Height)
                {
                    if (wlk.m_Entry.m_Pos.m_Height < hr.m_Min)
                        break;
                    if (nCount >= nMaxTxs)
                    {
                        bMore = true;
                        break;
                    }

                    hpLast.m_Height = wlk.m_Entry.m_Pos.m_Height;
                }
                else
                {
                    if (hpLast.m_Pos <= wlk.m_Entry.m_Pos.m_Pos)
                        continue; // already processed
                }

                hpLast.m_Pos = wlk.m_Entry.m_Pos.m_Pos;

                vInfo.clear();
                auto* pInfo = &ReadKrnInfo(vInfo, wlk);

                // read the whole block, including parent and all its nested
                while (pInfo->m_iParent)
                {
                    NodeDB::KrnInfo::Walker wlk2;
                    hpLast.m_Pos = pInfo->m_iParent;
                    _nodeBackend.get_DB().KrnInfoEnum(wlk2, hpLast, hpLast);
                    if (!wlk2.MoveNext())
                        OnKrnInfoCorrupted();

                    vInfo.clear();
                    pInfo = &ReadKrnInfo(vInfo, wlk2);
                }

                uint32_t nNumNested = pInfo->m_NumNested;
                if (pInfo->m_NumNested)
                {
                    vInfo.reserve(nNumNested + 1);

                    NodeDB::KrnInfo::Walker wlk2;
                    for (_nodeBackend.get_DB().KrnInfoEnum(wlk2, HeightPos(hpLast.m_Height, hpLast.m_Pos + 1), HeightPos(hpLast.m_Height, hpLast.m_Pos + nNumNested)); wlk2.MoveNext(); )
                        ReadKrnInfo(vInfo, wlk2);

                    if (vInfo.size() != nNumNested + 1)
                        OnKrnInfoCorrupted();

                    pInfo = &vInfo.front();
                    hpLast.m_Pos += nNumNested;
                }

                wrArr.OnContractInternal(*pInfo, &cid, 0, hpLast.m_Height);
            }

            json jTbl = MakeTable(std::move(wrArr.m_json));

            if (bMore)
            {
                json jMore = json::object();
                jMore["hMax"] = hpLast.m_Height - 1;
                jTbl["more"] = std::move(jMore);
            }

            wr.m_json["Calls history"] = std::move(jTbl);

        }

        out = std::move(wr.m_json);
    }

    static NodeProcessor::ContractInvokeExtraInfo& ReadKrnInfo(std::vector<NodeProcessor::ContractInvokeExtraInfo>& vec, NodeDB::KrnInfo::Walker& wlk)
    {
        auto& info = vec.emplace_back();

        Deserializer der;
        der.reset(wlk.m_Entry.m_Val.p, wlk.m_Entry.m_Val.n);
        der & info;

        info.m_Cid = wlk.m_Entry.m_Cid;
        return info;
    }

    json get_contract_details(const Blob& id, Height hMin, Height hMax, uint32_t nMaxTxs) override
    {
        if (id.n != bvm2::ContractID::nBytes)
            Exc::Fail("bad cid");

        json j;
        get_ContractState(j, *reinterpret_cast<const bvm2::ContractID*>(id.p), HeightRange(hMin, hMax), nMaxTxs);
        return j;
    }

    struct AssetHistoryWalker
    {
        typedef std::pair<Height, uint64_t> Pos;

        struct Event
            :public boost::intrusive::list_base_hook<>
        {
            Pos m_Pos;

            enum struct Type {
                Emit,
                Create,
                Destroy,
            };

            virtual ~Event() {}
            virtual Type get_Type() const = 0;

            typedef intrusive::list_autoclear<Event> List;
        };

        struct Event_Emit :public Event {
            NodeProcessor::AssetDataPacked m_Adp;
            virtual Type get_Type() const { return Type::Emit; }
        };

        struct Event_Destroy :public Event {
            virtual Type get_Type() const { return Type::Destroy; }
        };

        struct Event_Create :public Event {
            virtual Type get_Type() const { return Type::Create; }

            Asset::CreateInfo m_Ai;
            virtual ~Event_Create() {}
        };

        Event::List m_Lst;

        void Enum(NodeProcessor& p, Asset::ID aid, Height hMin, Height hMax, uint32_t nMaxOps)
        {
            if (!nMaxOps)
                return;

            NodeDB& db = p.get_DB();

            // in sqlite 64-bit nums are signed
            hMax = std::min<Height>(hMax, std::numeric_limits<int64_t>::max());
            hMin = std::min<Height>(hMin, std::numeric_limits<int64_t>::max());

            NodeDB::WalkerAssetEvt wlk;
            for (db.AssetEvtsEnumBwd(wlk, aid, hMax); wlk.MoveNext(); )
            {
                if ((m_Lst.size() >= nMaxOps) && (m_Lst.back().m_Pos.first != wlk.m_Height))
                    break;

                auto* pEvt = new Event_Emit;
                m_Lst.push_back(*pEvt);

                pEvt->m_Pos.first = wlk.m_Height;
                pEvt->m_Pos.second = wlk.m_Index;
                pEvt->m_Adp.set_Strict(wlk.m_Body);

                if (wlk.m_Height < hMin)
                    break;
            }

            auto it = m_Lst.begin();

            for (db.AssetEvtsEnumBwd(wlk, aid + Asset::s_MaxCount, hMax); wlk.MoveNext(); )
            {
                if (wlk.m_Height < hMin)
                    break;

                if ((m_Lst.size() >= nMaxOps * 2) && (m_Lst.back().m_Pos.first != wlk.m_Height))
                    break;

                Pos pos;
                pos.first = wlk.m_Height;
                pos.second = wlk.m_Index;

                while ((m_Lst.end() != it) && (it->m_Pos > pos))
                    it++;

                if (wlk.m_Body.n)
                {
                    auto* pEvt = new Event_Create;
                    m_Lst.insert(it, *pEvt);
                    pEvt->m_Pos = pos;

                    p.get_AssetCreateInfo(pEvt->m_Ai, wlk);
                }
                else
                {
                    auto* pEvt = new Event_Destroy;
                    m_Lst.insert(it, *pEvt);
                    pEvt->m_Pos = pos;
                }
            }
        }
    };

    json get_asset_history(Asset::ID aid, Height hMin, Height hMax, uint32_t nMaxOps) override
    {
        if (!aid)
            Exc::Fail("no aid");

        AssetHistoryWalker wlk;
        wlk.Enum(_nodeBackend, aid, hMin, hMax, nMaxOps);

        nMaxOps = std::min(nMaxOps, 2000u);

        ExtraInfo::Writer wrArr(json::array());
        wrArr.m_json.push_back(json::array({
            MakeTableHdr("Height"),
            MakeTableHdr("Event"),
            MakeTableHdr("Amount"),
            MakeTableHdr("Total Amount"),
            MakeTableHdr("Extra")
            }));

        Height hPrev = MaxHeight;
        uint32_t nCount = 0;
        bool bMore = false;
        for (auto it = wlk.m_Lst.begin(); wlk.m_Lst.end() != it; nCount++)
        {
            auto& x = *it;
            it++;

            if (hPrev != x.m_Pos.first)
            {
                if (nCount >= nMaxOps)
                {
                    bMore = true;
                    break;
                }

                hPrev = x.m_Pos.first;

                if (hPrev < hMin)
                    break;
            }

            ExtraInfo::Writer wrItem(json::array());
            wrItem.m_json.push_back(hPrev); // height

            typedef AssetHistoryWalker::Event::Type Type;

            switch (x.get_Type())
            {
            default:
                assert(false);
                // no break;

            case Type::Emit:
                {
                    auto& evt = Cast::Up<AssetHistoryWalker::Event_Emit>(x);
                    AmountBig::Type delta = evt.m_Adp.m_Amount;

                    if ((wlk.m_Lst.end() != it) && (it->get_Type() == Type::Emit))
                    {
                        AmountBig::Type v0 = Cast::Up<AssetHistoryWalker::Event_Emit>(*it).m_Adp.m_Amount;
                        v0.Negate();
                        delta += v0;
                    }

                    wrItem.m_json.push_back(delta.get_Msb() ? "Burn" : "Mint");
                    wrItem.m_json.push_back(MakeObjAmount(delta)); // handles negative sign
                    wrItem.m_json.push_back(MakeObjAmount(evt.m_Adp.m_Amount));
                    wrItem.m_json.push_back("");
                }
                break;

            case Type::Create:
                {
                    auto& evt = Cast::Up<AssetHistoryWalker::Event_Create>(x);
                    wrItem.m_json.push_back("Create");
                    wrItem.m_json.push_back("");
                    wrItem.m_json.push_back("");

                    ExtraInfo::Writer wrExtra;
                    wrExtra.AddAssetCreateInfo(evt.m_Ai);
                    wrItem.m_json.push_back(std::move(wrExtra.m_json));
                }
                break;

                case Type::Destroy:
                {
                    wrItem.m_json.push_back("Destroy");
                    wrItem.m_json.push_back("");
                    wrItem.m_json.push_back("");
                    wrItem.m_json.push_back("");
                }
                break;
            }

            wrArr.m_json.push_back(std::move(wrItem.m_json));

        }

        json jTbl = MakeTable(std::move(wrArr.m_json));

        if (bMore && (hPrev != MaxHeight))
        {
            json jMore = json::object();
            jMore["hMax"] = hPrev - 1;
            jTbl["more"] = std::move(jMore);
        }

        ExtraInfo::Writer wr;
        wr.m_json["Asset history"] = std::move(jTbl);
        return wr.m_json;
    }

    static uint32_t ExpanedNumWithCommas(char* szDst, const char* szSrc, uint32_t len)
    {
        const uint32_t nGroupLen = 3;
        // szDst and szSrc may be the same
        if (len)
        {
            uint32_t numCommas = (len - 1) / nGroupLen;
            uint32_t pos = len;
            len += numCommas;

            while (numCommas)
            {
                pos -= nGroupLen;
                memmove(szDst + pos + numCommas, szSrc + pos, nGroupLen);
                szDst[pos + (--numCommas)] = ',';
            }

            memmove(szDst, szSrc, pos);
        }

        szDst[len] = 0;
        return len;
    }

    uint32_t PrintDifficulty(char* sz, const Difficulty::Raw& d)
    {
        // print integer part only. Ok for mainnet.
        Difficulty::Raw dInt;
        dInt.SetDiv(d, uintBigFor<uint32_t>::Type(1u << Difficulty::s_MantissaBits));

        uint32_t len = dInt.PrintDecimal(sz);
        return ExpanedNumWithCommas(sz, sz, len);
    }

    bool extract_block_from_row(json& out, uint64_t row, Height height) {
        NodeDB& db = _nodeBackend.get_DB();

        Block::SystemState::Full blockState;
		Block::SystemState::ID id;
		Block::Body block;
		bool ok = true;
        std::vector<Output::Ptr> vOutsIn;

        ExtraInfo::ContractRichInfo cri;

        try {
            db.get_State(row, blockState);
			blockState.get_ID(id);

			NodeDB::StateID sid;
			sid.m_Row = row;
			sid.m_Height = id.m_Height;
			_nodeBackend.ExtractBlockWithExtra(block, vOutsIn, sid, cri.m_vInfo);

		} catch (...) {
            ok = false;
        }


        if (ok) {
            char buf[80];

            assert(block.m_vInputs.size() == vOutsIn.size());

            json inputs = json::array();
            for (size_t i = 0; i < block.m_vInputs.size(); i++)
            {
                const Input& inp = *block.m_vInputs[i];
                const Output& outp = *vOutsIn[i];
                assert(inp.m_Commitment == outp.m_Commitment);

                Height hCreate = inp.m_Internal.m_Maturity - outp.get_MinMaturity(0);

                json jItem = ExtraInfo::get(outp, hCreate, inp.m_Internal.m_Maturity, m_Mode);
                jItem["height"] = hCreate;

                inputs.push_back(std::move(jItem));
            }

            json outputs = json::array();
            for (const auto &v : block.m_vOutputs)
                outputs.push_back(ExtraInfo::get(*v, height, v->get_MinMaturity(height), m_Mode));

            json kernels = json::array();
            for (const auto &v : block.m_vKernels) {

                Amount fee = 0;
                json j = ExtraInfo::get(*v, fee, cri, height, m_Mode);

                if (Mode::Legacy == m_Mode)
                    j["fee"] = fee;
                else
                    j["fee"] = MakeObjAmount(fee);

                kernels.push_back(std::move(j));
            }


            json jAssets = json::array();
            jAssets.push_back(json::array({
                MakeTableHdr("Aid"),
                MakeTableHdr("Owner"),
                MakeTableHdr("Deposit"),
                MakeTableHdr("Supply"),
                MakeTableHdr("Lock height"),
                MakeTableHdr("Metadata")
                }));

            Asset::Full ai;
            for (ai.m_ID = 1; ; ai.m_ID++)
            {
                int ret = _nodeBackend.get_AssetAt(ai, height);
                if (!ret)
                    break;
                if (ret < 0)
                    continue;

                ExtraInfo::Writer wr(json::array());
                wr.m_json.push_back(MakeObjAid(ai.m_ID));

                wr.m_json.push_back(ExtraInfo::Writer::get_AssetOwner(ai));

                wr.m_json.push_back(MakeObjAmount(ai.m_Deposit));
                wr.m_json.push_back(MakeObjAmount(ai.m_Value));

                wr.m_json.push_back(ai.m_LockHeight);

                std::string s;
                ai.m_Metadata.get_String(s);
                wr.m_json.push_back(std::move(s));

                jAssets.push_back(std::move(wr.m_json));
            }

            auto btcRate = _exchangeRateProvider->getBeamTo(wallet::Currency::BTC(), blockState.m_Height);
            auto usdRate = _exchangeRateProvider->getBeamTo(wallet::Currency::USD(), blockState.m_Height);

            if (Mode::Legacy == m_Mode)
            {
                out = json{
                    {"found",      true},
                    {"timestamp",  blockState.m_TimeStamp},
                    {"height",     blockState.m_Height},
                    {"hash",       hash_to_hex(buf, id.m_Hash)},
                    {"prev",       hash_to_hex(buf, blockState.m_Prev)},
                    {"difficulty", blockState.m_PoW.m_Difficulty.ToFloat()},
                    {"chainwork",  uint256_to_hex(buf, blockState.m_ChainWork)},
                    {"subsidy",    Rules::get_Emission(blockState.m_Height)},
                    {"inputs",     inputs},
                    {"outputs",    outputs},
                    {"kernels",    kernels},
                    {"rate_btc",   btcRate},
                    {"rate_usd",   usdRate}
                };
            }
            else
            {
                json jInfo = json::array();

                jInfo.push_back({ MakeTableHdr("Height"), MakeObjHeight(id.m_Height) });
                jInfo.push_back({ MakeTableHdr("Timestamp"),MakeTypeObj("time", blockState.m_TimeStamp) });
                jInfo.push_back({ MakeTableHdr("Hash"), MakeObjBlob(id.m_Hash) });
                jInfo.push_back({ MakeTableHdr("Hash-Prev"), MakeObjBlob(blockState.m_Prev) });

                Difficulty::Raw d;
                blockState.m_PoW.m_Difficulty.Unpack(d);

                char szCw[ECC::uintBig::nTxtLen10Max / 3 * 4 + 2];
                PrintDifficulty(szCw, d);
                jInfo.push_back({ MakeTableHdr("Difficulty"), szCw });

                PrintDifficulty(szCw, blockState.m_ChainWork);
                jInfo.push_back({ MakeTableHdr("Chainwork"), szCw });
                jInfo.push_back({ MakeTableHdr("Subsidy"), MakeObjAmount(Rules::get_Emission(blockState.m_Height)) });

                out["info"] = MakeTable(std::move(jInfo));
            }

            out["assets"] = MakeTable(std::move(jAssets));
            out["inputs"] = std::move(inputs);
            out["outputs"] = std::move(outputs);
            out["kernels"] = std::move(kernels);

            LOG_DEBUG() << out;
        }
        return ok;
    }

    bool extract_block(json& out, Height height, uint64_t& row, uint64_t* prevRow) {
        bool ok = true;
        if (row == 0) {
            ok = extract_row(height, row, prevRow);
        } else if (prevRow != 0) {
            *prevRow = row;
            if (!_nodeBackend.get_DB().get_Prev(*prevRow)) {
                *prevRow = 0;
            }
        }
        return ok && extract_block_from_row(out, row, height);
    }

    json get_block_impl(uint64_t height, uint64_t& row, uint64_t* prevRow) {

        Height h = _nodeBackend.m_Cursor.m_Full.m_Height;

        if (height <= h)
        {
            json j;
            if (extract_block(j, height, row, prevRow))
                return j;
        }

        return json{ { "found", false}, {"height", height } };
    }

    json get_block(uint64_t height) override {
        uint64_t row=0;
        return get_block_impl(height, row, 0);
    }

    json get_block_by_kernel(const Blob& key) override {
        NodeDB& db = _nodeBackend.get_DB();

        Height height = db.FindKernel(key);
        uint64_t row = 0;

        return get_block_impl(height, row, 0);
    }

    json get_blocks(uint64_t startHeight, uint64_t n) override {

        json result = json::array();

        static const uint64_t maxElements = 1500;
        if (n > maxElements) n = maxElements;
        else if (n==0) n=1;


        Height endHeight = startHeight + n - 1;
        _exchangeRateProvider->preloadRates(startHeight, endHeight);

        uint64_t row = 0;
        uint64_t prevRow = 0;
        for (;;) {
            json j = get_block_impl(endHeight, row, &prevRow);
            if (endHeight == startHeight) {
                break;
            }
            result.push_back(std::move(j));
            row = prevRow;
            --endHeight;
        }
        return true;
    }

    json get_peers() override
    {
        auto& peers = _node.get_AcessiblePeerAddrs();
        json result = json::array();

        for (auto& peer : peers)
            result.push_back(peer.get_ParentObj().m_Addr.m_Value.str());

        return result;
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    json get_swap_offers() override
    {
        auto offers = _offersBulletinBoard->getOffersList();

        json result = json::array();
        for(auto& offer : offers)
        {
            result.push_back( json {
                {"status", offer.m_status},
                {"status_string", swapOfferStatusToString(offer.m_status)},
                {"txId", std::to_string(offer.m_txId)},
                {"beam_amount", std::to_string(wallet::PrintableAmount(offer.amountBeam(), true))},
                {"swap_amount", SwapAmountToString(offer.amountSwapCoin(), offer.swapCoinType())},
                {"swap_currency", std::to_string(offer.swapCoinType())},
                {"time_created", format_timestamp(wallet::kTimeStampFormat3x3, offer.timeCreated() * 1000, false)},
                {"min_height", offer.minHeight()},
                {"height_expired", offer.minHeight() + offer.peerResponseHeight()},
                {"is_beam_side", offer.isBeamSide()},
            });
        }

        return result;
    }

    json get_swap_totals() override
    {
        auto offers = _offersBulletinBoard->getOffersList();

        Amount beamAmount = 0,
               bitcoinAmount = 0,
               litecoinAmount = 0,
               qtumAmount = 0,
            //    bitcoinCashAmount = 0,
               dogecoinAmount = 0,
               dashAmount = 0,
               ethereumAmount = 0,
               daiAmount = 0,
               usdtAmount = 0,
               wbtcAmount = 0;

        for(auto& offer : offers)
        {
            beamAmount += offer.amountBeam();
            switch (offer.swapCoinType())
            {
                case wallet::AtomicSwapCoin::Bitcoin :
                    bitcoinAmount += offer.amountSwapCoin();
                    break;
                case wallet::AtomicSwapCoin::Litecoin :
                    litecoinAmount += offer.amountSwapCoin();
                    break;
                case wallet::AtomicSwapCoin::Qtum :
                    qtumAmount += offer.amountSwapCoin();
                    break;
                // case wallet::AtomicSwapCoin::Bitcoin_Cash :
                //     bitcoinCashAmount += offer.amountSwapCoin();
                //     break;
                case wallet::AtomicSwapCoin::Dogecoin :
                    dogecoinAmount += offer.amountSwapCoin();
                    break;
                case wallet::AtomicSwapCoin::Dash :
                    dashAmount += offer.amountSwapCoin();
                    break;
                case wallet::AtomicSwapCoin::Ethereum:
                    ethereumAmount += offer.amountSwapCoin();
                    break;
                case wallet::AtomicSwapCoin::Dai:
                    daiAmount += offer.amountSwapCoin();
                    break;
                case wallet::AtomicSwapCoin::Usdt:
                    usdtAmount += offer.amountSwapCoin();
                    break;
                case wallet::AtomicSwapCoin::WBTC:
                    wbtcAmount += offer.amountSwapCoin();
                    break;
                default :
                    LOG_ERROR() << "Unknown swap coin type";
                    return false;
            }
        }

        json obj = json{
            { "total_swaps_count", offers.size()},
            { "beams_offered", std::to_string(wallet::PrintableAmount(beamAmount, true)) },
            { "bitcoin_offered", SwapAmountToString(bitcoinAmount, wallet::AtomicSwapCoin::Bitcoin)},
            { "litecoin_offered", SwapAmountToString(litecoinAmount, wallet::AtomicSwapCoin::Litecoin)},
            { "qtum_offered", SwapAmountToString(qtumAmount, wallet::AtomicSwapCoin::Qtum)},
            // { "bicoin_cash_offered", SwapAmountToString(bitcoinCashAmount, wallet::AtomicSwapCoin::Bitcoin_Cash)},
            { "dogecoin_offered", SwapAmountToString(dogecoinAmount, wallet::AtomicSwapCoin::Dogecoin)},
            { "dash_offered", SwapAmountToString(dashAmount, wallet::AtomicSwapCoin::Dash)},
            { "ethereum_offered", SwapAmountToString(ethereumAmount, wallet::AtomicSwapCoin::Ethereum)},
            { "dai_offered", SwapAmountToString(daiAmount, wallet::AtomicSwapCoin::Dai)},
            { "usdt_offered", SwapAmountToString(usdtAmount, wallet::AtomicSwapCoin::Usdt)},
            { "wbtc_offered", SwapAmountToString(wbtcAmount, wallet::AtomicSwapCoin::WBTC)}
        };

        return obj;
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

    HttpMsgCreator _packer;

    // node db interface
	Node& _node;
    NodeProcessor& _nodeBackend;

    // helper fragments
    io::SharedBuffer _leftBrace, _comma, _rightBrace, _quote;

    // True if node is syncing at the moment
    bool _nodeIsSyncing;

    // node observers chain
    Node::IObserver** _hook;
    Node::IObserver* _nextHook;

    wallet::IWalletDB::Ptr _walletDB;
    wallet::Wallet::Ptr _wallet;
    std::shared_ptr<beam::BroadcastRouter> _broadcastRouter;
    std::shared_ptr<ExchangeRateProvider> _exchangeRateProvider;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    std::shared_ptr<wallet::OfferBoardProtocolHandler> _offerBoardProtocolHandler;
    wallet::SwapOffersBoard::Ptr _offersBulletinBoard;
    using WalletDbSubscriber = wallet::ScopedSubscriber<wallet::IWalletDbObserver, wallet::IWalletDB>;
    std::unique_ptr<WalletDbSubscriber> _walletDbSubscriber;
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
};

IAdapter::Ptr create_adapter(Node& node) {
    return IAdapter::Ptr(new Adapter(node));
}

}} //namespaces

