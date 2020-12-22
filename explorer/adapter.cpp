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
#include "http/http_msg_creator.h"
#include "http/http_json_serializer.h"
#include "nlohmann/json.hpp"
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
#include "wallet/client/extensions/offers_board/offers_protocol_handler.h"
#include "wallet/client/extensions/offers_board/swap_offers_board.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

namespace beam { namespace explorer {

namespace {

static const size_t PACKER_FRAGMENTS_SIZE = 4096;
static const size_t CACHE_DEPTH = 100000;

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
const unsigned int FAKE_SEED = 10283UL;
const char WALLET_DB_PATH[] = "explorer-wallet.db";
const char WALLET_DB_PASS[] = "1";
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

struct ResponseCache {
    io::SharedBuffer status;
    std::map<Height, io::SharedBuffer> blocks;
    Height currentHeight=0;

    explicit ResponseCache(size_t depth) : _depth(depth)
    {}

    void compact() {
        if (blocks.empty() || currentHeight <= _depth) return;
        Height horizon = currentHeight - _depth;
        if (blocks.rbegin()->first < horizon) {
            blocks.clear();
            return;
        }
        auto b = blocks.begin();
        auto it = b;
        while (it != blocks.end()) {
            if (it->first >= horizon) break;
            ++it;
        }
        blocks.erase(b, it);
    }

    bool get_block(io::SerializedMsg& out, Height h) {
        const auto& it = blocks.find(h);
        if (it == blocks.end()) return false;
        out.push_back(it->second);
        return true;
    }

    void put_block(Height h, const io::SharedBuffer& body) {
        if (currentHeight - h > _depth) return;
        compact();
        blocks[h] = body;
    }

private:
    size_t _depth;
};

using nlohmann::json;

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
        if (wallet::BroadcastMsgValidator::stringToPublicKey(wallet::kBroadcastValidatorPublicKey, key))
        {
            _validator.setPublisherKeys( { key } );
        }
        _broadcastGateway.registerListener(BroadcastContentType::ExchangeRates, this);
    }

    virtual ~ExchangeRateProvider() = default;

    std::string getRate(wallet::ExchangeRate::Currency unit, uint64_t height)
    {
        if (height >= _preloadStartHeight && height <= _preloadEndHeight)
        {
            auto it = std::find_if(
                _ratesCache.begin(), _ratesCache.end(),
                [unit, height] (const wallet::ExchangeRateHistoryEntity& rate) {
                    return rate.m_currency == wallet::ExchangeRate::Currency::Beam
                        && rate.m_unit == unit
                        && rate.m_height <= height;
                });
            return it != _ratesCache.end()
                ? std::to_string(wallet::PrintableAmount(it->m_rate, true))
                : "-";
        }
        else
        {
            const auto rate = _walletDB->getExchangeRateHistoryEntity(
                wallet::ExchangeRate::Currency::Beam, unit, height);
            return rate.m_height ? std::to_string(wallet::PrintableAmount(rate.m_rate, true)) : "-";
        }
    }

    void preloadRates(uint64_t startHeight, uint64_t endHeight)
    {
        if (endHeight < startHeight) return;
        _preloadStartHeight = startHeight;
        _preloadEndHeight = endHeight;

        const auto btcFirstRate = _walletDB->getExchangeRateHistoryEntity(
            wallet::ExchangeRate::Currency::Beam,
            wallet::ExchangeRate::Currency::Bitcoin,
            startHeight);

        const auto usdFirstRate = _walletDB->getExchangeRateHistoryEntity(
            wallet::ExchangeRate::Currency::Beam,
            wallet::ExchangeRate::Currency::Usd,
            startHeight);

        auto minHeight = std::min(btcFirstRate.m_height, usdFirstRate.m_height);

        _ratesCache = _walletDB->getExchangeRatesHistory(minHeight, endHeight);
    }

    // IBroadcastListener implementation
    bool onMessage(uint64_t unused, BroadcastMsg&& msg) override
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
                        wallet::ExchangeRateHistoryEntity rateHistory = rate;
                        rateHistory.m_height = blockState.m_Height;
                        _walletDB->saveExchangeRateHistoryEntity(rateHistory);
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
    std::vector<wallet::ExchangeRateHistoryEntity> _ratesCache;
};

/// Explorer server backend, gets callback on status update and returns json messages for server
class Adapter : public Node::IObserver, public IAdapter {
public:
    Adapter(Node& node) :
        _packer(PACKER_FRAGMENTS_SIZE),
		_node(node),
        _nodeBackend(node.get_Processor()),
        _statusDirty(true),
        _nodeIsSyncing(true),
        _cache(CACHE_DEPTH)
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

        _broadcastRouter = std::make_shared<BroadcastRouter>(*nnet, *wnet);
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
        if (isSyncing != _nodeIsSyncing) {
            _statusDirty = true;
            _nodeIsSyncing = isSyncing;
        }
        if (_nextHook) _nextHook->OnSyncProgress();
    }

    void OnStateChanged() override {
        const auto& cursor = _nodeBackend.m_Cursor;
        _cache.currentHeight = cursor.m_Sid.m_Height;
        _statusDirty = true;
        if (_nextHook) _nextHook->OnStateChanged();
    }

    void OnRolledBack(const Block::SystemState::ID& id) override {

        auto& blocks = _cache.blocks;

        blocks.erase(blocks.lower_bound(id.m_Height), blocks.end());

        if (_nextHook) _nextHook->OnRolledBack(id);
    }

    bool get_status(io::SerializedMsg& out) override {
        if (_statusDirty) {
            const auto& cursor = _nodeBackend.m_Cursor;
            _cache.currentHeight = cursor.m_Sid.m_Height;

            double possibleShieldedReadyHours = 0;
            uint64_t shieldedPer24h = 0;

            if (_cache.currentHeight)
            {
                NodeDB& db = _nodeBackend.get_DB();
                auto shieldedByLast24h =
                    db.ShieldedOutpGet(_cache.currentHeight >= 1440 ? _cache.currentHeight - 1440 : 1);
                auto averageWindowBacklog = Rules::get().Shielded.MaxWindowBacklog / 2;

                if (shieldedByLast24h && shieldedByLast24h != _nodeBackend.m_Extra.m_ShieldedOutputs)
                {
                    shieldedPer24h = _nodeBackend.m_Extra.m_ShieldedOutputs - shieldedByLast24h;
                    possibleShieldedReadyHours = ceil(averageWindowBacklog / (double)shieldedPer24h * 24);
                }
            }

            char buf[80];

            _sm.clear();
            if (!serialize_json_msg(
                _sm,
                _packer,
                json{
                    { "timestamp", cursor.m_Full.m_TimeStamp },
                    { "height", _cache.currentHeight },
                    { "low_horizon", _nodeBackend.m_Extra.m_TxoHi },
                    { "hash", hash_to_hex(buf, cursor.m_ID.m_Hash) },
                    { "chainwork",  uint256_to_hex(buf, cursor.m_Full.m_ChainWork) },
                    { "peers_count", _node.get_AcessiblePeerCount() },
                    { "shielded_outputs_total", _nodeBackend.m_Extra.m_ShieldedOutputs },
                    { "shielded_outputs_per_24h", shieldedPer24h },
                    { "shielded_possible_ready_in_hours", shieldedPer24h ? std::to_string(possibleShieldedReadyHours) : "-" }
                }
            )) {
                return false;
            }

            _cache.status = io::normalize(_sm, false);
            _statusDirty = false;
            _sm.clear();
        }
        out.push_back(_cache.status);
        return true;
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

    struct ExtraInfo
    {
        struct Writer
        {
            std::ostringstream m_os;
            bool m_Empty = true;

            void Next()
            {
                if (m_Empty)
                    m_Empty = false;
                else
                    m_os << ", ";
            }

            void OnAsset(const Asset::Proof* pProof)
            {
                if (pProof)
                {
                    Next();
                    auto t0 = pProof->m_Begin;
                    m_os << "Asset [" << t0 << "-" << t0 + Rules::get().CA.m_ProofCfg.get_N() - 1 << "]";
                }
            }
        };

        static std::string get(const Asset::Metadata& md)
        {
            std::string sMetadata;
            const ByteBuffer& bb = md.m_Value; // alias
            sMetadata.reserve(bb.size());
            for (size_t i = 0; i < bb.size(); i++)
            {
                char ch = bb[i];
                if ((ch < 32) || (ch > 126))
                    ch = '?';
                sMetadata.push_back(ch);
            }

            return sMetadata;
        }

        static std::string get(const Output& outp, Height h, Height hMaturity)
        {
            Writer w;

            if (outp.m_Coinbase)
            {
                w.Next();
                w.m_os << "Coinbase";
            }

            if (outp.m_pPublic)
            {
                w.Next();
                w.m_os << "Value=" << outp.m_pPublic->m_Value;
            }

            if (outp.m_Incubation)
            {
                w.Next();
                w.m_os << "Incubation +" << outp.m_Incubation;
            }

            if (hMaturity != h)
            {
                w.Next();
                w.m_os << "Maturity=" << hMaturity;
            }

            w.OnAsset(outp.m_pAsset.get());

            return w.m_os.str();
        }

        static std::string get(const TxKernel& krn, Amount& fee)
        {
            struct MyWalker
                :public TxKernel::IWalker
            {
                Writer m_Wr;
                Amount m_Fee = 0;

                virtual bool OnKrn(const TxKernel& krn) override
                {
                    m_Fee += krn.m_Fee;

                    switch (krn.get_Subtype())
                    {
#define THE_MACRO(id, name) case id: OnKrnEx(Cast::Up<TxKernel##name>(krn)); break;
                        BeamKernelsAll(THE_MACRO)
#undef THE_MACRO
                    }

                    return true;
                }

                void OnKrnEx(const TxKernelStd& krn)
                {
                    if (krn.m_pRelativeLock)
                    {
                        m_Wr.Next();
                        m_Wr.m_os << "Rel.Lock ID=" << krn.m_pRelativeLock->m_ID << " H=" << krn.m_pRelativeLock->m_LockHeight;
                    }

                    if (krn.m_pHashLock)
                    {
                        m_Wr.Next();
                        m_Wr.m_os << "Hash.Lock Preimage=" << krn.m_pHashLock->m_Value;
                    }
                }

                void OnKrnEx(const TxKernelAssetCreate& krn)
                {
                    m_Wr.Next();
                    m_Wr.m_os << "Asset.Create MD.Hash=" << krn.m_MetaData.m_Hash;
                }

                void OnKrnEx(const TxKernelAssetDestroy& krn)
                {
                    m_Wr.Next();
                    m_Wr.m_os << "Asset.Destroy ID=" << krn.m_AssetID;
                }

                void OnKrnEx(const TxKernelAssetEmit& krn)
                {
                    m_Wr.Next();
                    m_Wr.m_os << "Asset.Emit ID=" << krn.m_AssetID << " Value=" << krn.m_Value;
                }

                void OnKrnEx(const TxKernelShieldedOutput& krn)
                {
                    m_Wr.Next();
                    m_Wr.m_os << "Shielded.Out";
                    m_Wr.OnAsset(krn.m_Txo.m_pAsset.get());
                }

                void OnKrnEx(const TxKernelShieldedInput& krn)
                {
                    uint32_t n = krn.m_SpendProof.m_Cfg.get_N();

                    TxoID id0 = krn.m_WindowEnd;
                    if (id0 > n)
                        id0 -= n;
                    else
                        id0 = 0;

                    m_Wr.Next();
                    m_Wr.m_os << "Shielded.In Set=[" << id0 << "-" << krn.m_WindowEnd - 1 << "]";
                    m_Wr.OnAsset(krn.m_pAsset.get());
                }

            } wlk;

            if (!krn.m_vNested.empty())
            {
                wlk.m_Wr.Next();
                wlk.m_Wr.m_os << "Composite";
            }

            wlk.Process(krn);
            fee = wlk.m_Fee;

            return wlk.m_Wr.m_os.str();
        }
    };


    bool extract_block_from_row(json& out, uint64_t row, Height height) {
        NodeDB& db = _nodeBackend.get_DB();

        Block::SystemState::Full blockState;
		Block::SystemState::ID id;
		Block::Body block;
		bool ok = true;
        std::vector<Output::Ptr> vOutsIn;

        try {
            db.get_State(row, blockState);
			blockState.get_ID(id);

			NodeDB::StateID sid;
			sid.m_Row = row;
			sid.m_Height = id.m_Height;
			_nodeBackend.ExtractBlockWithExtra(block, vOutsIn, sid);

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

                inputs.push_back(
                json{
                    {"commitment", uint256_to_hex(buf, outp.m_Commitment.m_X)},
                    {"height",   hCreate},
                    {"extra",  ExtraInfo::get(outp, hCreate, inp.m_Internal.m_Maturity)}
                }
                );
            }

            json outputs = json::array();
            for (const auto &v : block.m_vOutputs) {
                outputs.push_back(
                json{
                    {"commitment", uint256_to_hex(buf, v->m_Commitment.m_X)},
                    {"extra",  ExtraInfo::get(*v, height, v->get_MinMaturity(height))}
                }
                );
            }

            json kernels = json::array();
            for (const auto &v : block.m_vKernels) {

                Amount fee = 0;
                std::string sExtra = ExtraInfo::get(*v, fee);

                kernels.push_back(
                    json{
                        {"id", hash_to_hex(buf, v->m_Internal.m_ID)},
                        {"minHeight", v->m_Height.m_Min},
                        {"maxHeight", v->m_Height.m_Max},
                        {"fee", fee},
                        {"extra",  sExtra}
                    }
                );
            }

            json assets = json::array();
            Asset::Full ai;
            for (ai.m_ID = 1; ; ai.m_ID++)
            {
                int ret = _nodeBackend.get_AssetAt(ai, height);
                if (!ret)
                    break;

                if (ret > 0)
                {
                    assets.push_back(
                        json{
                            {"id", ai.m_ID},
                            {"metadata", ExtraInfo::get(ai.m_Metadata)},
                            {"metahash", hash_to_hex(buf, ai.m_Metadata.m_Hash)},
                            {"owner", hash_to_hex(buf, ai.m_Owner)},
                            {"value_lo", AmountBig::get_Lo(ai.m_Value)},
                            {"value_hi", AmountBig::get_Hi(ai.m_Value)},
                            {"lock_height",  ai.m_LockHeight}
                        }
                    );
                }
            }

            auto btcRate = _exchangeRateProvider->getRate(wallet::ExchangeRate::Currency::Bitcoin, blockState.m_Height);
            auto usdRate = _exchangeRateProvider->getRate(wallet::ExchangeRate::Currency::Usd, blockState.m_Height);

            out = json{
                {"found",      true},
                {"timestamp",  blockState.m_TimeStamp},
                {"height",     blockState.m_Height},
                {"hash",       hash_to_hex(buf, id.m_Hash)},
                {"prev",       hash_to_hex(buf, blockState.m_Prev)},
                {"difficulty", blockState.m_PoW.m_Difficulty.ToFloat()},
                {"chainwork",  uint256_to_hex(buf, blockState.m_ChainWork)},
                {"subsidy",    Rules::get_Emission(blockState.m_Height)},
                {"assets",     assets},
                {"inputs",     inputs},
                {"outputs",    outputs},
                {"kernels",    kernels},
                {"rate_btc",   btcRate},
                {"rate_usd",   usdRate}

            };

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

    bool get_block_impl(io::SerializedMsg& out, uint64_t height, uint64_t& row, uint64_t* prevRow) {
        if (_cache.get_block(out, height)) {
            if (prevRow && row > 0) {
                extract_row(height, row, prevRow);
            }
            return true;
        }

        if (_statusDirty) {
            const auto &cursor = _nodeBackend.m_Cursor;
            _cache.currentHeight = cursor.m_Sid.m_Height;
        }

        io::SharedBuffer body;
        bool blockAvailable = (height <= _cache.currentHeight);
        if (blockAvailable) {
            json j;
            if (!extract_block(j, height, row, prevRow)) {
                blockAvailable = false;
            } else {
                _sm.clear();
                if (serialize_json_msg(_sm, _packer, j)) {
                    body = io::normalize(_sm, false);
                    _cache.put_block(height, body);
                } else {
                    return false;
                }
                _sm.clear();
            }
        }

        if (blockAvailable) {
            out.push_back(body);
            return true;
        }

        return serialize_json_msg(out, _packer, json{ { "found", false}, {"height", height } });
    }

    bool json2Msg(const json& obj, io::SerializedMsg& out) {
        LOG_DEBUG() << obj;

        _sm.clear();
        io::SharedBuffer body;
        if (serialize_json_msg(_sm, _packer, obj)) {
            body = io::normalize(_sm, false);
        } else {
            return false;
        }
        _sm.clear();

        out.push_back(body);

        return true;
    }

    bool get_block(io::SerializedMsg& out, uint64_t height) override {
        uint64_t row=0;
        return get_block_impl(out, height, row, 0);
    }

    bool get_block_by_hash(io::SerializedMsg& out, const ByteBuffer& hash) override {
        NodeDB& db = _nodeBackend.get_DB();

        Height height = db.FindBlock(hash);
        uint64_t row = 0;

        return get_block_impl(out, height, row, 0);
    }

    bool get_block_by_kernel(io::SerializedMsg& out, const ByteBuffer& key) override {
        NodeDB& db = _nodeBackend.get_DB();

        Height height = db.FindKernel(key);
        uint64_t row = 0;

        return get_block_impl(out, height, row, 0);
    }

    bool get_blocks(io::SerializedMsg& out, uint64_t startHeight, uint64_t n) override {
        static const uint64_t maxElements = 1500;
        if (n > maxElements) n = maxElements;
        else if (n==0) n=1;
        Height endHeight = startHeight + n - 1;
        _exchangeRateProvider->preloadRates(startHeight, endHeight);
        out.push_back(_leftBrace);
        uint64_t row = 0;
        uint64_t prevRow = 0;
        for (;;) {
            bool ok = get_block_impl(out, endHeight, row, &prevRow);
            if (!ok) return false;
            if (endHeight == startHeight) {
                break;
            }
            out.push_back(_comma);
            row = prevRow;
            --endHeight;
        }
        out.push_back(_rightBrace);
        return true;
    }

    bool get_peers(io::SerializedMsg& out) override
    {
        auto& peers = _node.get_AcessiblePeerAddrs();

        out.push_back(_leftBrace);

        for (auto& peer : peers)
        {
            auto addr = peer.get_ParentObj().m_Addr.m_Value.str();

            {
                out.push_back(_quote);
                out.push_back({ addr.data(), addr.size() });
                out.push_back(_quote);
            }

            out.push_back(_comma);
        }

        // remove last comma
        if (!peers.empty())
            out.pop_back();

        out.push_back(_rightBrace);

        return true;
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    bool get_swap_offers(io::SerializedMsg& out) override
    {
        auto offers = _offersBulletinBoard->getOffersList();

        json result = json::array();
        for(auto& offer : offers)
        {
            result.push_back( json {
                {"status", offer.m_status},
                {"status_string", swapOfferStatusToString(offer.m_status)},
                {"txId", wallet::TxIDToString(offer.m_txId)},
                {"beam_amount", std::to_string(wallet::PrintableAmount(offer.amountBeam(), true))},
                {"swap_amount", std::to_string(wallet::PrintableAmount(offer.amountSwapCoin(), true))},
                {"swap_currency", std::to_string(offer.swapCoinType())},
                {"time_created", format_timestamp(wallet::kTimeStampFormat3x3, offer.timeCreated() * 1000, false)},
                {"min_height", offer.minHeight()},
                {"height_expired", offer.minHeight() + offer.peerResponseHeight()},
            });
        }

        return json2Msg(result, out);
    }

    bool get_swap_totals(io::SerializedMsg& out) override
    {
        auto offers = _offersBulletinBoard->getOffersList();

        Amount beamAmount = 0,
               bitcoinAmount = 0,
               litecoinAmount = 0,
               qtumAmount = 0,
            //    bitcoinCashAmount = 0,
               dogecoinAmount = 0,
               dashAmount = 0;

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
                default :
                    LOG_ERROR() << "Unknown swap coin type";
                    return false;
            }
        }

        json obj = json{
            { "total_swaps_count", offers.size()},
            { "beams_offered", std::to_string(wallet::PrintableAmount(beamAmount, true)) },
            { "bitcoin_offered", std::to_string(wallet::PrintableAmount(bitcoinAmount, true))},
            { "litecoin_offered", std::to_string(wallet::PrintableAmount(litecoinAmount, true))},
            { "qtum_offered", std::to_string(wallet::PrintableAmount(qtumAmount, true))},
            // { "bicoin_cash_offered", std::to_string(wallet::PrintableAmount(bitcoinCashAmount, true))},
            { "dogecoin_offered", std::to_string(wallet::PrintableAmount(dogecoinAmount, true))},
            { "dash_offered", std::to_string(wallet::PrintableAmount(dashAmount, true))}
        };

        return json2Msg(obj, out);
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

    HttpMsgCreator _packer;

    // node db interface
	Node& _node;
    NodeProcessor& _nodeBackend;

    // helper fragments
    io::SharedBuffer _leftBrace, _comma, _rightBrace, _quote;

    // If true then status boby needs to be refreshed
    bool _statusDirty;

    // True if node is syncing at the moment
    bool _nodeIsSyncing;

    // node observers chain
    Node::IObserver** _hook;
    Node::IObserver* _nextHook;

    ResponseCache _cache;

    io::SerializedMsg _sm;

    wallet::IWalletDB::Ptr _walletDB;
    wallet::Wallet::Ptr _wallet;
    std::shared_ptr<wallet::BroadcastRouter> _broadcastRouter;
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

