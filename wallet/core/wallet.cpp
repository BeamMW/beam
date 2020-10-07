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

#include "wallet.h"
#include <boost/uuid/uuid.hpp>

#include "core/ecc_native.h"
#include "core/block_crypt.h"
#include "core/shielded.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include "simple_transaction.h"
#include "strings_resources.h"
#include "assets_utils.h"

#include <algorithm>
#include <random>
#include <iomanip>
#include <numeric>

namespace beam::wallet
{
    using namespace std;
    using namespace ECC;

    namespace
    {
        bool ApplyTransactionParameters(BaseTransaction::Ptr tx, const PackedTxParameters& parameters, bool isInternalSource)
        {
            bool txChanged = false;
            SubTxID subTxID = kDefaultSubTxID;
            for (const auto& p : parameters)
            {
                if (p.first == TxParameterID::SubTxIndex)
                {
                    // change subTxID
                    Deserializer d;
                    d.reset(p.second.data(), p.second.size());
                    d& subTxID;
                    continue;
                }

                if (!isInternalSource && !tx->IsTxParameterExternalSettable(p.first, subTxID))
                {
                    LOG_WARNING() << tx->GetTxID() << "Attempt to set internal tx parameter: " << static_cast<int>(p.first);
                    continue;
                }

                txChanged |= tx->GetWalletDB()->setTxParameter(tx->GetTxID(), subTxID, p.first, p.second, true, isInternalSource);
            }
            return txChanged;
        }

        Timestamp RestoreCreationTime(const Block::SystemState::Full& tip, Height confirmHeight)
        {
            Timestamp ts = tip.m_TimeStamp;
            if (tip.m_Height > confirmHeight)
            {
                auto delta = (tip.m_Height - confirmHeight);
                ts -= delta * Rules::get().DA.Target_s;
            }
            else if (tip.m_Height < confirmHeight)
            {
                auto delta = confirmHeight - tip.m_Height;
                ts += delta * Rules::get().DA.Target_s;
            }
            return ts;
        }
    }

    // @param SBBS address as string
    // Returns whether the address is a valid SBBS address i.e. a point on an ellyptic curve
    bool CheckReceiverAddress(const std::string& addr)
    {
        WalletID walletID;
        return
            walletID.FromHex(addr) &&
            walletID.IsValid();
    }

    void TestSenderAddress(const TxParameters& parameters, IWalletDB::Ptr walletDB)
    {
        const auto& myID = parameters.GetParameter<WalletID>(TxParameterID::MyID);
        if (!myID)
        {
            throw InvalidTransactionParametersException("No MyID");
        }

        auto senderAddr = walletDB->getAddress(*myID);
        if (!senderAddr || !senderAddr->isOwn() || senderAddr->isExpired())
        {
            throw SenderInvalidAddressException();
        }
    }

    TxParameters ProcessReceiverAddress(const TxParameters& parameters, IWalletDB::Ptr walletDB, bool isMandatory)
    {
        const auto& peerID = parameters.GetParameter<WalletID>(TxParameterID::PeerID);
        if (!peerID)
        {
            if (isMandatory)
                throw InvalidTransactionParametersException("No PeerID");

            return parameters;
        }

        auto receiverAddr = walletDB->getAddress(*peerID);
        if (receiverAddr)
        {
            if (receiverAddr->isOwn() && receiverAddr->isExpired())
            {
                LOG_ERROR() << "Can't send to the expired address.";
                throw ReceiverAddressExpiredException();
            }

            // update address comment if changed
            if (auto message = parameters.GetParameter(TxParameterID::Message); message)
            {
                auto messageStr = std::string(message->begin(), message->end());
                if (messageStr != receiverAddr->m_label)
                {
                    receiverAddr->m_label = messageStr;
                    walletDB->saveAddress(*receiverAddr);
                }
            }
            
            TxParameters temp{ parameters };
            temp.SetParameter(TxParameterID::IsSelfTx, receiverAddr->isOwn());
            return temp;
        }
        else
        {
            WalletAddress address;
            address.m_walletID = *peerID;
            address.m_createTime = getTimestamp();
            if (auto message = parameters.GetParameter(TxParameterID::Message); message)
            {
                address.m_label = std::string(message->begin(), message->end());
            }
            if (auto identity = parameters.GetParameter<PeerID>(TxParameterID::PeerWalletIdentity); identity)
            {
                address.m_Identity = *identity;
            }
            
            walletDB->saveAddress(address);
        }
        return parameters;
    }

    const char Wallet::s_szNextEvt[] = "NextUtxoEvent"; // any event, not just UTXO. The name is for historical reasons

    Wallet::Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action, UpdateCompletedAction&& updateCompleted)
        : m_WalletDB{ walletDB }
        , m_TxCompletedAction{ move(action) }
        , m_UpdateCompleted{ move(updateCompleted) }
        , m_LastSyncTotal(0)
        , m_OwnedNodesOnline(0)
    {
        assert(walletDB);
        // the only default type of transaction
        RegisterTransactionType(TxType::Simple, make_unique<SimpleTransaction::Creator>(m_WalletDB));
    }

    Wallet::~Wallet()
    {
        CleanupNetwork();
    }

    void Wallet::CleanupNetwork()
    {
        // clear all requests
#define THE_MACRO(type, msgOut, msgIn) \
                while (!m_Pending##type.empty()) \
                    DeleteReq(*m_Pending##type.begin());

        REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO

        m_MessageEndpoints.clear();
        m_NodeEndpoint = nullptr;
    }

    // Fly client implementation
    void Wallet::get_Kdf(Key::IKdf::Ptr& pKdf)
    {
        pKdf = m_WalletDB->get_MasterKdf();
    }

    void Wallet::get_OwnerKdf(Key::IPKdf::Ptr& ownerKdf)
    {
        ownerKdf = m_WalletDB->get_OwnerKdf();
    }

    // Implementation of the FlyClient protocol method
    // @id : PeerID - peer id of the node
    // bUp : bool - flag indicating that node is online
    void Wallet::OnOwnedNode(const PeerID& id, bool bUp)
    {
        if (bUp)
        {
            if (!m_OwnedNodesOnline++) // on first connection to the node
                RequestEvents(); // maybe time to refresh UTXOs
        }
        else
        {
            assert(m_OwnedNodesOnline); // check that m_OwnedNodesOnline is positive number
            if (!--m_OwnedNodesOnline)
                AbortEvents();
        }

        for (const auto sub : m_subscribers)
        {
            sub->onOwnedNode(id, bUp);
        }
    }

    Block::SystemState::IHistory& Wallet::get_History()
    {
        return m_WalletDB->get_History();
    }

    void Wallet::SetNodeEndpoint(std::shared_ptr<proto::FlyClient::INetwork> nodeEndpoint)
    {
        m_NodeEndpoint = std::move(nodeEndpoint);
    }

    void Wallet::AddMessageEndpoint(IWalletMessageEndpoint::Ptr endpoint)
    {
        m_MessageEndpoints.insert(endpoint);
    }

    // Rescan the blockchain for UTXOs and shielded coins
    void Wallet::Rescan()
    {
        AbortEvents();

        // We save all Incoming coins of active transactions and
        // restore them after clearing db. This will save our outgoing & available amounts
        std::vector<Coin> ocoins;
        // do the same thing with shielded coins which are in progress
        std::vector<ShieldedCoin> shieldedCoins;
        for (const auto& tx : m_ActiveTransactions)
        {
            const auto& txocoins = m_WalletDB->getCoinsCreatedByTx(tx.first);
            ocoins.insert(ocoins.end(), txocoins.begin(), txocoins.end());
            auto shieldedCoin = m_WalletDB->getShieldedCoin(tx.first);

            // save newly created coins only to be able to accomplish the transaction,
            // all the others should be able to be restored from blockchain
            if (shieldedCoin && shieldedCoin->m_createTxId && *shieldedCoin->m_createTxId == tx.first)
            {
                shieldedCoins.push_back(*shieldedCoin);
            }
        }

        m_WalletDB->clearCoins();
        m_WalletDB->clearShieldedCoins();

        // Restore Incoming coins of active transactions
        m_WalletDB->saveCoins(ocoins);
        // Restore shielded coins
        for (const auto& sc : shieldedCoins)
        {
            m_WalletDB->saveShieldedCoin(sc);
        }

        storage::setVar(*m_WalletDB, s_szNextEvt, 0);
        RequestEvents();
    }

    void Wallet::RegisterTransactionType(TxType type, BaseTransaction::Creator::Ptr creator)
    {
        m_TxCreators[type] = move(creator);
    }

    TxID Wallet::StartTransaction(const TxParameters& parameters)
    {
        auto tx = ConstructTransactionFromParameters(parameters);
        if (!tx)
        {
            throw FailToStartNewTransactionException();
        }
        ProcessTransaction(tx);
        return *parameters.GetTxID();
    }

    void Wallet::ProcessTransaction(wallet::BaseTransaction::Ptr tx)
    {
        MakeTransactionActive(tx);
        UpdateTransaction(tx->GetTxID());
    }

    void Wallet::ResumeTransaction(const TxDescription& tx)
    {
        if (tx.canResume() && m_ActiveTransactions.find(tx.m_txId) == m_ActiveTransactions.end())
        {
            auto t = ConstructTransaction(tx.m_txId, tx.m_txType);
            if (t)
            {
                MakeTransactionActive(t);
                UpdateOnSynced(t);
            }
        }
    }

    void Wallet::VisitActiveTransaction(const TxVisitor& visitor)
    {
        for(const auto& it: m_ActiveTransactions)
        {
            visitor(it.first, it.second);
        }
    }

    void Wallet::ResumeAllTransactions()
    {
        auto txs = m_WalletDB->getTxHistory(TxType::ALL);
        for (auto& tx : txs)
        {
            ResumeTransaction(tx);
        }
    }

    bool Wallet::IsWalletInSync() const
    {
        Block::SystemState::Full state;
        get_tip(state);

        return IsValidTimeStamp(state.m_TimeStamp);
    }

    size_t Wallet::GetUnsafeActiveTransactionsCount() const
    {
        return std::count_if(m_ActiveTransactions.begin(), m_ActiveTransactions.end(), [](const auto& p)
            {
                return p.second && !p.second->IsInSafety();
            });
    }

    void Wallet::OnAsyncStarted()
    {
        if (m_AsyncUpdateCounter == 0)
        {
            LOG_VERBOSE() << "Async update started!";
        }
        ++m_AsyncUpdateCounter;
    }

    void Wallet::OnAsyncFinished()
    {
        if (--m_AsyncUpdateCounter == 0)
        {
            LOG_VERBOSE() << "Async update finished!";
            if (m_UpdateCompleted)
            {
                m_UpdateCompleted();
            }
        }
    }

    void Wallet::on_tx_completed(const TxID& txID)
    {
        // Note: the passed TxID is (most probably) the member of the transaction, 
        // which we, most probably, are going to erase from the map, which can potentially delete it.
        // Make sure we either copy the txID, or prolong the lifetime of the tx.

        BaseTransaction::Ptr pGuard;

        auto it = m_ActiveTransactions.find(txID);
        if (it != m_ActiveTransactions.end())
        {
            pGuard.swap(it->second);
            m_ActiveTransactions.erase(it);
            m_NextTipTransactionToUpdate.erase(pGuard);
            m_TransactionsToUpdate.erase(pGuard);
            pGuard->FreeResources();
        }

        if (m_TxCompletedAction)
        {
            m_TxCompletedAction(txID);
        }
    }

    void Wallet::on_tx_failed(const TxID& txID)
    {
        on_tx_completed(txID);
    }

    bool Wallet::MyRequestUtxo::operator < (const MyRequestUtxo& x) const
    {
        return m_Msg.m_Utxo < x.m_Msg.m_Utxo;
    }

    bool Wallet::MyRequestKernel::operator < (const MyRequestKernel& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestKernel2::operator < (const MyRequestKernel2& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestAsset::operator < (const MyRequestAsset& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestTransaction::operator < (const MyRequestTransaction& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestEvents::operator < (const MyRequestEvents& x) const
    {
        return false;
    }

    bool Wallet::MyRequestProofShieldedOutp::operator < (const MyRequestProofShieldedOutp& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestShieldedList::operator < (const MyRequestShieldedList& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestStateSummary::operator < (const MyRequestStateSummary& x) const
    {
        return false;
    }

    void Wallet::RequestHandler::OnComplete(Request& r)
    {
        uint32_t n = get_ParentObj().SyncRemains();

        switch (r.get_Type())
        {
#define THE_MACRO(type, msgOut, msgIn) \
        case Request::Type::type: \
            { \
                MyRequest##type& x = static_cast<MyRequest##type&>(r); \
                get_ParentObj().DeleteReq(x); \
                get_ParentObj().OnRequestComplete(x); \
            } \
            break;

            REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO

        default:
            assert(false);
        }

        if (n)
            get_ParentObj().CheckSyncDone();
    }

    // Implementation of the INegotiatorGateway::confirm_kernel
    // @param txID : TxID - transaction id
    // @param kernelID : Merkle::Hash& - kernel id
    // @param subTxID : SubTxID - in case of the complex transaction, there could be sub transactions
    void Wallet::confirm_kernel(const TxID& txID, const Merkle::Hash& kernelID, SubTxID subTxID)
    {
        if (auto it = m_ActiveTransactions.find(txID); it != m_ActiveTransactions.end())
        {
            // check if we have already asked for kernel on given height
            Height lastUnconfirmedHeight = 0;
            if (it->second->GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight, subTxID) && lastUnconfirmedHeight > 0)
            {
                Block::SystemState::Full state;
                if (!get_tip(state) || state.m_Height == lastUnconfirmedHeight)
                {
                    UpdateOnNextTip(it->second);
                    return;
                }
            }

            MyRequestKernel::Ptr pVal(new MyRequestKernel);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            pVal->m_Msg.m_ID = kernelID;

            if (PostReqUnique(*pVal))
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for kernel: " << pVal->m_Msg.m_ID;
        }
    }

    void Wallet::confirm_asset(const TxID& txID, const PeerID& ownerID, SubTxID subTxID)
    {
        if (auto it = m_ActiveTransactions.find(txID); it != m_ActiveTransactions.end())
        {
            MyRequestAsset::Ptr pVal(new MyRequestAsset);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            pVal->m_Msg.m_Owner = ownerID;
            pVal->m_Msg.m_AssetID = Asset::s_InvalidID;

            if (PostReqUnique(*pVal))
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for asset with the owner ID: " << ownerID;
        }
    }

    void Wallet::confirm_asset(const TxID& txID, Asset::ID assetId, SubTxID subTxID)
    {
        if (auto it = m_ActiveTransactions.find(txID); it != m_ActiveTransactions.end())
        {
            MyRequestAsset::Ptr pVal(new MyRequestAsset);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            pVal->m_Msg.m_Owner = Asset::s_InvalidOwnerID;
            pVal->m_Msg.m_AssetID = assetId;

            if (PostReqUnique(*pVal))
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get proof for asset with id: " << assetId;
        }
    }

    // Implementation of the INegotiatorGateway::get_kernel
    // @param txID : TxID - transaction id
    // @param kernelID : Merkle::Hash& - kernel id
    // @param subTxID : SubTxID - in case of the complex transaction, there could be sub transactions
    void Wallet::get_kernel(const TxID& txID, const Merkle::Hash& kernelID, SubTxID subTxID)
    {
        if (auto it = m_ActiveTransactions.find(txID); it != m_ActiveTransactions.end())
        {
            MyRequestKernel2::Ptr pVal(new MyRequestKernel2);
            pVal->m_TxID = txID;
            pVal->m_SubTxID = subTxID;
            auto& msg = pVal->m_Msg; // alias
            msg.m_Fetch = true;
            msg.m_ID = kernelID;

            if (PostReqUnique(*pVal))
            {
                LOG_INFO() << txID << "[" << subTxID << "]" << " Get details for kernel: " << msg.m_ID;
            }
        }
    }

    // Implementation of the INegotiatorGateway::get_tip
    bool Wallet::get_tip(Block::SystemState::Full& state) const
    {
        return m_WalletDB->get_History().get_Tip(state);
    }

    // Implementation of the INegotiatorGateway::send_tx_params
    void Wallet::send_tx_params(const WalletID& peerID, const SetTxParameter& msg)
    {
        for (auto& endpoint : m_MessageEndpoints)
        {
            endpoint->Send(peerID, msg);
        }
    }

    // Implementation of the INegotiatorGateway::get_shielded_list
    void Wallet::get_shielded_list(const TxID& txId, TxoID startIndex, uint32_t count, ShieldedListCallback&& callback)
    {
        MyRequestShieldedList::Ptr pVal(new MyRequestShieldedList);
        pVal->m_callback = std::move(callback);
        pVal->m_TxID = txId;

        pVal->m_Msg.m_Id0 = startIndex;
        pVal->m_Msg.m_Count = count;

        if (PostReqUnique(*pVal))
        {
            LOG_INFO() << txId << " Get shielded list, start_index = " << startIndex << ", count = " << count;
        }
    }

    void Wallet::get_proof_shielded_output(const TxID& txId, const ECC::Point& serialPublic, ProofShildedOutputCallback&& callback)
    {
        MyRequestProofShieldedOutp::Ptr pVal(new MyRequestProofShieldedOutp);
        pVal->m_callback = std::move(callback);
        pVal->m_TxID = txId;

        pVal->m_Msg.m_SerialPub = serialPublic;

        if (PostReqUnique(*pVal))
        {
            LOG_INFO() << txId << " Get proof of shielded output.";
        }
    }

    // Implementation of the INegotiatorGateway::UpdateOnNextTip
    void Wallet::UpdateOnNextTip(const TxID& txID)
    {
        auto it = m_ActiveTransactions.find(txID);
        if (it != m_ActiveTransactions.end())
        {
            UpdateOnNextTip(it->second);
        }
    }

    Wallet::VoucherManager::Request* Wallet::VoucherManager::CreateIfNew(const WalletID& trg)
    {
        Request::Target key;
        key.m_Value = trg;
        if (m_setTrg.end() != m_setTrg.find(key))
            return nullptr;

        ECC::Scalar::Native nonce;
        nonce.GenRandomNnz();

        Request* pReq = new Request;
        pReq->m_Target.m_Value = trg;
        m_setTrg.insert(pReq->m_Target);

        pReq->m_OwnAddr.m_Pk.FromSk(nonce);
        pReq->m_OwnAddr.SetChannelFromPk();

        for (auto& p : get_ParentObj().m_MessageEndpoints)
            p->Listen(pReq->m_OwnAddr, nonce);

        return pReq;
    }

    void Wallet::VoucherManager::Delete(Request& r)
    {
        for (auto& p : get_ParentObj().m_MessageEndpoints)
            p->Unlisten(r.m_OwnAddr);

        m_setTrg.erase(Request::Target::Set::s_iterator_to(r.m_Target));
        delete &r;
    }

    void Wallet::VoucherManager::DeleteAll()
    {
        while (!m_setTrg.empty())
            Delete(m_setTrg.begin()->get_ParentObj());
    }

    void Wallet::get_UniqueVoucher(const WalletID& peerID, const TxID&, boost::optional<ShieldedTxo::Voucher>& res)
    {
        auto count = m_WalletDB->getVoucherCount(peerID);
        const size_t VoucherCountThreshold = 5;

        if (count < VoucherCountThreshold)
        {
            VoucherManager::Request* pReq = m_VoucherManager.CreateIfNew(peerID);
            if (pReq)
                RequestVouchersFrom(peerID, pReq->m_OwnAddr, 30);
        }

        if (count > 0)
            res = m_WalletDB->grabVoucher(peerID);
    }

    void Wallet::SendSpecialMsg(const WalletID& peerID, SetTxParameter& msg)
    {
        memset0(&msg.m_TxID.front(), msg.m_TxID.size());
        send_tx_params(peerID, msg);
    }

    void Wallet::RequestVouchersFrom(const WalletID& peerID, const WalletID& myID, uint32_t nCount /* = 1 */)
    {
        SetTxParameter msg;
        msg.m_From = myID;
        msg.m_Type = TxType::VoucherRequest;

        msg.AddParameter((TxParameterID) 0, nCount);

        SendSpecialMsg(peerID, msg);
    }

    void Wallet::OnSpecialMsg(const WalletID& myID, const SetTxParameter& msg)
    {
        switch (msg.m_Type)
        {
        case TxType::VoucherRequest:
            {
                auto pKeyKeeper = m_WalletDB->get_KeyKeeper();
                if (!pKeyKeeper             // We can generate the ticket with OwnerKey, but can't sign it.
                 || !m_OwnedNodesOnline)    // The wallet has no ability to recognoize received shielded coin
                {
                    FailVoucherRequest(msg.m_From, myID);
                    return; 
                }

                uint32_t nCount = 0;
                msg.GetParameter((TxParameterID) 0, nCount);

                if (!nCount)
                {
                    FailVoucherRequest(msg.m_From, myID);
                    return; //?!
                }

                auto address = m_WalletDB->getAddress(myID);
                if (!address.is_initialized() || !address->m_OwnID)
                    return;

                auto res = GenerateVoucherList(pKeyKeeper, address->m_OwnID, nCount);

                SetTxParameter msgOut;
                msgOut.m_Type = TxType::VoucherResponse;
                msgOut.m_From = myID;
                msgOut.AddParameter(TxParameterID::ShieldedVoucherList, std::move(res));

                SendSpecialMsg(msg.m_From, msgOut);

            }
            break;

        case TxType::VoucherResponse:
            {
                std::vector<ShieldedTxo::Voucher> res;
                msg.GetParameter(TxParameterID::ShieldedVoucherList, res);
                if (res.empty())
                {
                    LOG_WARNING() << "Received an empty voucher list";
                    FailTxWaitingForVouchers(msg.m_From);
                    return;
                }

                auto address = m_WalletDB->getAddress(msg.m_From);
                if (!address.is_initialized())
                {
                    LOG_WARNING() << "Received vouchers for unknown address";
                    FailTxWaitingForVouchers(msg.m_From);
                    return;
                }

                if (!IsValidVoucherList(res, address->m_Identity))
                {
                    LOG_WARNING() << "Invalid voucher list received";
                    FailTxWaitingForVouchers(msg.m_From);
                    return;
                }

                OnVouchersFrom(*address, myID, std::move(res));

            }
            break;

        default: // suppress watning
            break;
        }
    }

    std::vector<BaseTransaction::Ptr> Wallet::FindTxWaitingForVouchers(const WalletID& peerID) const
    {
        std::vector<BaseTransaction::Ptr> res;
        for (auto p : m_ActiveTransactions)
        {
            ShieldedTxo::Voucher voucher;
            const auto& tx = p.second;
            if (tx->GetType() == TxType::PushTransaction
                && tx->GetMandatoryParameter<WalletID>(TxParameterID::PeerID) == peerID
                && !tx->GetParameter<ShieldedTxo::Voucher>(TxParameterID::Voucher, voucher))
            {
                res.push_back(tx);
            }
        }
        return res;
    }

    void Wallet::FailTxWaitingForVouchers(const WalletID& peerID)
    {
        auto transactions = FindTxWaitingForVouchers(peerID);
        for (auto tx : transactions)
        {
            tx->SetParameter(TxParameterID::FailureReason, TxFailureReason::CannotGetVouchers);
            UpdateTransaction(tx);
        }
    }

    void Wallet::FailVoucherRequest(const WalletID& peerID, const WalletID& myID)
    {
        SetTxParameter msgOut;
        msgOut.m_Type = TxType::VoucherResponse;
        msgOut.m_From = myID;
        msgOut.AddParameter(TxParameterID::FailureReason, TxFailureReason::CannotGetVouchers);

        SendSpecialMsg(peerID, msgOut);
    }

    void Wallet::OnVouchersFrom(const WalletAddress& addr, const WalletID& ownAddr, std::vector<ShieldedTxo::Voucher>&& res)
    {
        VoucherManager::Request::Target key;
        key.m_Value = addr.m_walletID;

        VoucherManager::Request::Target::Set::iterator it = m_VoucherManager.m_setTrg.find(key);
        if (m_VoucherManager.m_setTrg.end() == it)
            return;
        VoucherManager::Request& r = it->get_ParentObj();

        if (r.m_OwnAddr != ownAddr)
            return;

        m_VoucherManager.Delete(r);

        for (const auto& v : res)
            m_WalletDB->saveVoucher(v, addr.m_walletID);

        auto transactions = FindTxWaitingForVouchers(addr.m_walletID);
        for (auto tx : transactions)
        {
            UpdateTransaction(tx);
        }
    }

    void Wallet::OnWalletMessage(const WalletID& myID, const SetTxParameter& msg)
    {
        if (memis0(&msg.m_TxID.front(), msg.m_TxID.size()))
        {
            // special command/request
            try
            {
                OnSpecialMsg(myID, msg);
            }
            catch (const std::exception& exc)
            {
                LOG_WARNING() << "Special msg failed: " << exc.what();
            }
        }
        else {
            OnTransactionMsg(myID, msg);
        }
    }

    void Wallet::OnRequestComplete(MyRequestTransaction& r)
    {
        LOG_DEBUG() << r.m_TxID << "[" << r.m_SubTxID << "]" << " register status " << static_cast<uint32_t>(r.m_Res.m_Value);

        auto it = m_ActiveTransactions.find(r.m_TxID);
        if (it != m_ActiveTransactions.end())
        {
            it->second->SetParameter(TxParameterID::TransactionRegistered, r.m_Res.m_Value, r.m_SubTxID);
            it->second->SetParameter(TxParameterID::TransactionRegisteredInternal, r.m_Res.m_Value, r.m_SubTxID);
            UpdateTransaction(r.m_TxID);
        }
    }

    bool Wallet::CanCancelTransaction(const TxID& txId) const
    {
        if (auto it = m_ActiveTransactions.find(txId); it != m_ActiveTransactions.end())
        {
            return it->second->CanCancel();
        }
        return false;
    }

    void Wallet::CancelTransaction(const TxID& txId)
    {
        LOG_INFO() << txId << " Canceling tx";

        if (auto it = m_ActiveTransactions.find(txId); it != m_ActiveTransactions.end())
        {
            it->second->Cancel();
        }
        else
        {
            LOG_WARNING() << "Transaction already inactive";
        }
    }

    void Wallet::DeleteTransaction(const TxID& txId)
    {
        LOG_INFO() << "deleting tx " << txId;
        if (auto it = m_ActiveTransactions.find(txId); it == m_ActiveTransactions.end())
        {
            m_WalletDB->deleteTx(txId);
        }
        else
        {
            LOG_WARNING() << "Cannot delete running transaction";
        }
    }

    void Wallet::UpdateTransaction(const TxID& txID)
    {
        auto it = m_ActiveTransactions.find(txID);
        if (it != m_ActiveTransactions.end())
        {
            UpdateTransaction(it->second);
        }
        else
        {
            LOG_DEBUG() << txID << " Unexpected event";
        }
    }

    void Wallet::UpdateTransaction(BaseTransaction::Ptr tx)
    {
        bool bSynced = !SyncRemains() && IsNodeInSync();

        if (bSynced)
        {
            AsyncContextHolder holder(*this);
            tx->Update();
        }
        else
        {
            UpdateOnSynced(tx);
        }
    }

    void Wallet::UpdateOnSynced(BaseTransaction::Ptr tx)
    {
        m_TransactionsToUpdate.insert(tx);
    }

    void Wallet::UpdateOnNextTip(BaseTransaction::Ptr tx)
    {
        m_NextTipTransactionToUpdate.insert(tx);
    }

    void Wallet::OnRequestComplete(MyRequestUtxo& r)
    {
        if (r.m_Res.m_Proofs.empty())
            return; // Right now nothing is concluded from empty proofs

        const auto& proof = r.m_Res.m_Proofs.front(); // Currently - no handling for multiple coins for the same commitment.
        // we don't know the real height, but it'll be used for logging only. For standard outputs maturity and height are the same
        ProcessEventUtxo(r.m_CoinID, proof.m_State.m_Maturity, proof.m_State.m_Maturity, true, {});
    }

    void Wallet::OnRequestComplete(MyRequestKernel& r)
    {
        auto it = m_ActiveTransactions.find(r.m_TxID);
        if (m_ActiveTransactions.end() == it)
        {
            return;
        }
        auto tx = it->second;
        if (!r.m_Res.m_Proof.empty())
        {
            m_WalletDB->get_History().AddStates(&r.m_Res.m_Proof.m_State, 1); // why not?

            if (tx->SetParameter(TxParameterID::KernelProofHeight, r.m_Res.m_Proof.m_State.m_Height, r.m_SubTxID))
            {
                UpdateTransaction(tx);
            }
        }
        else
        {
            Block::SystemState::Full sTip;
            get_tip(sTip);
            tx->SetParameter(TxParameterID::KernelUnconfirmedHeight, sTip.m_Height, r.m_SubTxID);
            UpdateTransaction(tx);
        }
    }

    void Wallet::OnRequestComplete(MyRequestAsset& req)
    {
        const auto it = m_ActiveTransactions.find(req.m_TxID);
        if (m_ActiveTransactions.end() == it)
        {
            return;
        }

        Block::SystemState::Full sTip;
        get_tip(sTip);
        auto tx = it->second;

        if (!req.m_Res.m_Proof.empty())
        {
            const auto& info  = req.m_Res.m_Info;
            const auto height = m_WalletDB->getCurrentHeight();
            m_WalletDB->saveAsset(info, height);

            tx->SetParameter(TxParameterID::AssetConfirmedHeight, height, req.m_SubTxID);
            tx->SetParameter(TxParameterID::AssetInfoFull, info, req.m_SubTxID);
            tx->SetParameter(TxParameterID::AssetUnconfirmedHeight, Height(0), req.m_SubTxID);

            if (tx->GetType() == TxType::AssetReg)
            {
                m_WalletDB->markAssetOwned(info.m_ID);
            }

            stringstream ss;
            ss << req.m_TxID << "[" << req.m_SubTxID << "]";
            const auto prefix = ss.str();
            LOG_INFO() << prefix << " Received proof for Asset with ID " << info.m_ID;

            if(const auto wasset = m_WalletDB->findAsset(info.m_ID))
            {
                wasset->LogInfo(req.m_TxID, req.m_SubTxID);
            }

            UpdateTransaction(tx);
        }
        else
        {
            const auto& assetId = req.m_Msg.m_AssetID;
            if (assetId != Asset::s_InvalidID)
            {
                m_WalletDB->dropAsset(assetId);
            }
            else
            {
                const auto& assetOwner = req.m_Msg.m_Owner;
                if (assetOwner != Asset::s_InvalidOwnerID)
                {
                    m_WalletDB->dropAsset(assetOwner);
                }
            }

            tx->SetParameter(TxParameterID::AssetConfirmedHeight, Height(0), req.m_SubTxID);
            tx->SetParameter(TxParameterID::AssetInfoFull, Asset::Full(), req.m_SubTxID);
            tx->SetParameter(TxParameterID::AssetUnconfirmedHeight, sTip.m_Height, req.m_SubTxID);
            UpdateTransaction(tx);
        }
    }

    void Wallet::OnRequestComplete(MyRequestKernel2& r)
    {
        auto it = m_ActiveTransactions.find(r.m_TxID);
        if (m_ActiveTransactions.end() == it)
        {
            return;
        }
        auto tx = it->second;

        if (r.m_Res.m_Kernel)
        {
            tx->SetParameter(TxParameterID::Kernel, r.m_Res.m_Kernel, r.m_SubTxID);
            tx->SetParameter(TxParameterID::KernelProofHeight, r.m_Res.m_Height, r.m_SubTxID);
        }
        else
        {
            Block::SystemState::Full sTip;
            get_tip(sTip);
            tx->SetParameter(TxParameterID::KernelUnconfirmedHeight, sTip.m_Height, r.m_SubTxID);
        }
    }

    void Wallet::OnRequestComplete(MyRequestShieldedList& r)
    {
        r.m_callback(r.m_Msg.m_Id0, r.m_Msg.m_Count, r.m_Res);
    }

    void Wallet::OnRequestComplete(MyRequestProofShieldedInp& r)
    {
        // TODO(alex.starun): implement this
    }

    void Wallet::OnRequestComplete(MyRequestProofShieldedOutp& r)
    {
        r.m_callback(r.m_Res); // either successful or not
    }

    void Wallet::OnRequestComplete(MyRequestBbsMsg& r)
    {
        assert(false);
    }

    void Wallet::OnRequestComplete(MyRequestStateSummary& r)
    {
        // TODO: save full response?
        m_WalletDB->set_ShieldedOuts(r.m_Res.m_ShieldedOuts);
    }

    void Wallet::RequestEvents()
    {
        if (!m_OwnedNodesOnline)
            return;

        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        Height h = GetEventsHeightNext();
        assert(h <= sTip.m_Height + 1);
        if (h > sTip.m_Height)
            return;

        if (!m_PendingEvents.empty())
        {
            if (m_PendingEvents.begin()->m_Msg.m_HeightMin == h)
                return; // already pending
            DeleteReq(*m_PendingEvents.begin());
        }

        MyRequestEvents::Ptr pReq(new MyRequestEvents);
        pReq->m_Msg.m_HeightMin = h;
        PostReqUnique(*pReq);
    }

    void Wallet::AbortEvents()
    {
        if (!m_PendingEvents.empty())
            DeleteReq(*m_PendingEvents.begin());
    }

    void Wallet::OnRequestComplete(MyRequestEvents& r)
    {
        struct MyParser
            :public proto::Event::IGroupParser
        {
            Wallet& m_This;
            MyParser(Wallet& x) :m_This(x) {}

            virtual void OnEventType(proto::Event::Shielded& evt) override
            {
                m_This.ProcessEventShieldedUtxo(evt, m_Height);
            }

            virtual void OnEventType(proto::Event::AssetCtl& evt) override
            {
                m_This.ProcessEventAsset(evt, m_Height);
            }

            virtual void OnEventType(proto::Event::Utxo& evt) override
            {
                // filter-out false positives
                if (!m_This.m_WalletDB->IsRecoveredMatch(evt.m_Cid, evt.m_Commitment))
                    return;

                bool bAdd = 0 != (proto::Event::Flags::Add & evt.m_Flags);
                m_This.ProcessEventUtxo(evt.m_Cid, m_Height, evt.m_Maturity, bAdd, evt.m_User);
            }

        } p(*this);
        
        uint32_t nCount = p.Proceed(r.m_Res.m_Events);

        if (nCount < r.m_Max)
        {
            Block::SystemState::Full sTip;
            m_WalletDB->get_History().get_Tip(sTip);

            SetEventsHeight(sTip.m_Height);
        }
        else
        {
            SetEventsHeight(p.m_Height);
            RequestEvents(); // maybe more events pending
        }
    }

    void Wallet::SetEventsHeight(Height h)
    {
        uintBigFor<Height>::Type var;
        var = h + 1; // we're actually saving the next
        storage::setVar(*m_WalletDB, s_szNextEvt, var);
    }

    Height Wallet::GetEventsHeightNext()
    {
        uintBigFor<Height>::Type var;
        if (!storage::getVar(*m_WalletDB, s_szNextEvt, var))
            return 0;

        Height h;
        var.Export(h);
        return h;
    }

    void Wallet::ProcessEventUtxo(const CoinID& cid, Height h, Height hMaturity, bool bAdd, const Output::User& user)
    {
        Coin c;
        c.m_ID = cid;
        bool bExists = m_WalletDB->findCoin(c);
        c.m_maturity = hMaturity;


        const auto* data = Output::User::ToPacked(user);
        if (!memis0(data->m_TxID.m_pData, sizeof(TxID)))
        {
            c.m_createTxId.emplace();
            std::copy_n(data->m_TxID.m_pData, sizeof(TxID), c.m_createTxId->begin());
        }

        LOG_INFO() << "CoinID: " << c.m_ID << " Maturity=" << hMaturity << (bAdd ? " Confirmed" : " Spent") << ", Height=" << h;

        if (bAdd)
        {
            std::setmin(c.m_confirmHeight, h); // in case of std utxo proofs - the event height may be bigger than actual utxo height

            // Check if this Coin participates in any active transaction
            // if it does and mark it as outgoing (bug: ux_504)
            for (const auto& [txid, txptr] : m_ActiveTransactions)
            {
                std::vector<Coin::ID> icoins;
                txptr->GetParameter(TxParameterID::InputCoins, icoins);
                if (std::find(icoins.begin(), icoins.end(), c.m_ID) != icoins.end())
                {
                    c.m_status = Coin::Status::Outgoing;
                    c.m_spentTxId = txid;
                    LOG_INFO() << "CoinID: " << c.m_ID << " marked as Outgoing";
                }
            }
        }
        else
        {
            if (!bExists)
                return; // should alert!

            std::setmin(c.m_spentHeight, h); // reported spend height may be bigger than it actuall was (in case of macroblocks)
        }

        m_WalletDB->saveCoin(c);
    }

    void Wallet::ProcessEventAsset(const proto::Event::AssetCtl& assetCtl, Height h)
    {
        // TODO
    }

    void Wallet::ProcessEventShieldedUtxo(const proto::Event::Shielded& shieldedEvt, Height h)
    {
        auto shieldedCoin = m_WalletDB->getShieldedCoin(shieldedEvt.m_CoinID.m_Key);
        if (!shieldedCoin)
        {
            shieldedCoin = ShieldedCoin{};
        }

        shieldedCoin->m_CoinID = shieldedEvt.m_CoinID;
        shieldedCoin->m_TxoID = shieldedEvt.m_TxoID;

        bool isAdd = 0 != (proto::Event::Flags::Add & shieldedEvt.m_Flags);
        if (isAdd)
        {
            shieldedCoin->m_confirmHeight = std::min(shieldedCoin->m_confirmHeight, h);
        }
        else
        {
            shieldedCoin->m_spentHeight = std::min(shieldedCoin->m_spentHeight, h);
        }

        m_WalletDB->saveShieldedCoin(*shieldedCoin);

        LOG_INFO() << "Shielded output, ID: " << shieldedEvt.m_TxoID << (isAdd ? " Confirmed" : " Spent") << ", Height=" << h;
        RestoreTransactionFromShieldedCoin(*shieldedCoin);
    }

    void Wallet::OnRolledBack()
    {
        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        Block::SystemState::ID id;
        sTip.get_ID(id);
        LOG_INFO() << "Rolled back to " << id;

        m_WalletDB->get_History().DeleteFrom(sTip.m_Height + 1);
        m_WalletDB->rollbackConfirmedUtxo(sTip.m_Height);
        m_WalletDB->rollbackConfirmedShieldedUtxo(sTip.m_Height);
        m_WalletDB->rollbackAssets(sTip.m_Height);

        // Rollback active transaction
        for (auto it = m_ActiveTransactions.begin(); m_ActiveTransactions.end() != it; it++)
        {
            const auto& pTx = it->second;
            if (pTx->Rollback(sTip.m_Height))
            {
                UpdateOnSynced(pTx);
            }
        }

        // Rollback inactive (completed or active) transactions if applicable
        auto txs = m_WalletDB->getTxHistory(TxType::ALL); // get list of ALL transactions
        for (auto& tx : txs)
        {
            // For all transactions that are not currently in the 'active' tx list
            if (m_ActiveTransactions.find(tx.m_txId) == m_ActiveTransactions.end())
            {
                // Reconstruct tx with reset parameters and add it to the active list
                auto pTx = ConstructTransaction(tx.m_txId, tx.m_txType);
                if (pTx && pTx->Rollback(sTip.m_Height))
                {
                    m_ActiveTransactions.emplace(tx.m_txId, pTx);
                    UpdateOnSynced(pTx);
                }
            }
        }

        Height h = GetEventsHeightNext();
        if (h > sTip.m_Height + 1)
        {
            SetEventsHeight(sTip.m_Height);
        }
    }

    void Wallet::OnEventsSerif(const Hash::Value& hv, Height h)
    {
        static const char szEvtSerif[] = "EventsSerif";

        HeightHash hh;
        if (!storage::getVar(*m_WalletDB, szEvtSerif, hh))
        {
            hh.m_Hash = Zero;
            // If this is the 1st time we received the serif - we do NOT assume the wallet was synced ok. It can potentially be already out-of-sync.
            // Hence once node and wallet are both upgraded from older version - there will always be initial rescan.
            hh.m_Height = MaxHeight;
        }

        bool bHashChanged = (hh.m_Hash != hv);
        bool bMustRescan = bHashChanged;
        if (bHashChanged)
        {
            // Node Serif has changed (either connected to different node, or it rescanned the blockchain). The events stream may not be consistent with ours.
            Height h0 = GetEventsHeightNext();
            if (!h0)
                bMustRescan = false; // nothing to rescan atm
            else
            {
                if (h0 > hh.m_Height)
                {
                    // Our events are consistent and full up to height h0-1.
                    bMustRescan = (h >= h0);
                }
            }

            hh.m_Hash = hv;
        }

        if (bHashChanged || (h != hh.m_Height))
        {
            LOG_INFO() << "Events Serif changed: " << (bHashChanged ? "new Hash, " : "") << "Height=" << h << (bMustRescan ? ", Resyncing" : "");

            hh.m_Height = h;
            storage::setVar(*m_WalletDB, szEvtSerif, hh);
        }

        if (bMustRescan)
            Rescan();

    }

    void Wallet::OnNewPeer(const PeerID& id, io::Address address)
    {
        constexpr size_t MaxPeers = 10;
        std::deque<io::Address> addresses;
        storage::getBlobVar(*m_WalletDB, FallbackPeers, addresses);
        addresses.push_back(address);
        if (addresses.size() > MaxPeers)
        {
            addresses.pop_front();
        }
        storage::setBlobVar(*m_WalletDB, FallbackPeers, addresses);
    }

    void Wallet::OnNewTip()
    {
        m_WalletDB->ShrinkHistory();

        Block::SystemState::Full sTip;
        get_tip(sTip);
        if (!sTip.m_Height)
            return; //?!

        Block::SystemState::ID id;
        sTip.get_ID(id);
        LOG_INFO() << "Sync up to " << id;

        RequestEvents();
        RequestStateSummary();

        for (auto& tx : m_NextTipTransactionToUpdate)
        {
            UpdateOnSynced(tx);
        }
        m_NextTipTransactionToUpdate.clear();

        CheckSyncDone();

        ProcessStoredMessages();
    }

    void Wallet::OnTipUnchanged()
    {
        LOG_INFO() << "Tip has not been changed";

        CheckSyncDone();

        ProcessStoredMessages();
    }

    void Wallet::getUtxoProof(const Coin& coin)
    {
        MyRequestUtxo::Ptr pReq(new MyRequestUtxo);
        pReq->m_CoinID = coin.m_ID;

        if (!m_WalletDB->get_CommitmentSafe(pReq->m_Msg.m_Utxo, coin.m_ID))
        {
            LOG_WARNING() << "You cannot get utxo commitment without private key";
            return;
        }

        LOG_DEBUG() << "Get utxo proof: " << pReq->m_Msg.m_Utxo;

        PostReqUnique(*pReq);
    }

    uint32_t Wallet::SyncRemains() const
    {
        size_t val =
#define THE_MACRO(type) m_Pending##type.size() +
            REQUEST_TYPES_Sync(THE_MACRO)
#undef THE_MACRO
            0;

        return static_cast<uint32_t>(val);
    }

    void Wallet::CheckSyncDone()
    {
        report_sync_progress();

        if (SyncRemains())
            return;

        m_LastSyncTotal = 0;

        saveKnownState();
    }

    void Wallet::saveKnownState()
    {
        Block::SystemState::Full sTip;
        get_tip(sTip);

        Block::SystemState::ID id;
        if (sTip.m_Height)
            sTip.get_ID(id);
        else
            ZeroObject(id);

        m_WalletDB->setSystemStateID(id);
        LOG_INFO() << "Current state is " << id;
        notifySyncProgress();

        if (!IsValidTimeStamp(sTip.m_TimeStamp))
        {
            // we are not ready to process transactions
            return;
        }
        std::unordered_set<BaseTransaction::Ptr> txSet;
        txSet.swap(m_TransactionsToUpdate);

        if (!txSet.empty())
        {
            AsyncContextHolder async(*this);
            for (auto it = txSet.begin(); txSet.end() != it; it++)
            {
                BaseTransaction::Ptr pTx = *it;
                if (m_ActiveTransactions.find(pTx->GetTxID()) != m_ActiveTransactions.end())
                    pTx->Update();
            }
        }
    }

    void Wallet::notifySyncProgress()
    {
        uint32_t n = SyncRemains();
        for (const auto sub : m_subscribers)
        {
            sub->onSyncProgress(m_LastSyncTotal - n, m_LastSyncTotal);
        }
    }

    void Wallet::report_sync_progress()
    {
        if (!m_LastSyncTotal)
            return;

        uint32_t nDone = m_LastSyncTotal - SyncRemains();
        assert(nDone <= m_LastSyncTotal);
        int p = static_cast<int>((nDone * 100) / m_LastSyncTotal);
        LOG_INFO() << "Synchronizing with node: " << p << "% (" << nDone << "/" << m_LastSyncTotal << ")";

        notifySyncProgress();
    }

    void Wallet::SendTransactionToNode(const TxID& txId, Transaction::Ptr data, SubTxID subTxID)
    {
        LOG_DEBUG() << txId << "[" << subTxID << "]" << " sending tx for registration";

#ifndef NDEBUG
        TxBase::Context::Params pars;
        TxBase::Context ctx(pars);
        ctx.m_Height.m_Min = m_WalletDB->getCurrentHeight();
        assert(data->IsValid(ctx));
#endif // NDEBUG

        MyRequestTransaction::Ptr pReq(new MyRequestTransaction);
        pReq->m_TxID = txId;
        pReq->m_SubTxID = subTxID;
        pReq->m_Msg.m_Transaction = std::move(data);

        PostReqUnique(*pReq);
    }

    void Wallet::register_tx(const TxID& txId, Transaction::Ptr data, SubTxID subTxID)
    {
        SendTransactionToNode(txId, data, subTxID);
    }

    void Wallet::Subscribe(IWalletObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);

        m_WalletDB->Subscribe(observer);
    }

    void Wallet::Unsubscribe(IWalletObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);

        m_WalletDB->Unsubscribe(observer);
    }

    void Wallet::OnTransactionMsg(const WalletID& myID, const SetTxParameter& msg)
    {
        auto it = m_ActiveTransactions.find(msg.m_TxID);
        if (it != m_ActiveTransactions.end())
        {
            const auto& pTx = it->second;

            if (pTx->GetType() != msg.m_Type)
            {
                LOG_WARNING() << msg.m_TxID << " Parameters for invalid tx type";
                return;
            }

            WalletID peerID;
            if (pTx->GetParameter(TxParameterID::PeerID, peerID))
            {
                if (peerID != msg.m_From)
                    return; // if we already have PeerID, we should ignore messages from others
            }
            else
            {
                pTx->SetParameter(TxParameterID::PeerID, msg.m_From, false);
            }

            if (ApplyTransactionParameters(pTx, msg.m_Parameters, false))
                UpdateTransaction(pTx);

            return;
        }

        TxType type = TxType::Simple;
        if (storage::getTxParameter(*m_WalletDB, msg.m_TxID, TxParameterID::TransactionType, type))
            // we return only active transactions
            return;

        if (msg.m_Type == TxType::AtomicSwap)
            return; // we don't create swap from SBBS message

        bool isSender = false;
        if (!msg.GetParameter(TxParameterID::IsSender, isSender) || isSender == true)
            return;

        BaseTransaction::Ptr pTx = ConstructTransaction(msg.m_TxID, msg.m_Type);
        if (!pTx)
            return;

        pTx->SetParameter(TxParameterID::TransactionType, msg.m_Type, false);
        pTx->SetParameter(TxParameterID::CreateTime, getTimestamp(), false);
        pTx->SetParameter(TxParameterID::MyID, myID, false);
        pTx->SetParameter(TxParameterID::PeerID, msg.m_From, false);
        pTx->SetParameter(TxParameterID::IsInitiator, false, false);
        pTx->SetParameter(TxParameterID::Status, TxStatus::Pending, true);

        auto address = m_WalletDB->getAddress(myID);
        if (address.is_initialized())
        {
            ByteBuffer message(address->m_label.begin(), address->m_label.end());
            pTx->SetParameter(TxParameterID::Message, message);
        }

        MakeTransactionActive(pTx);
        ApplyTransactionParameters(pTx, msg.m_Parameters, false);

        UpdateTransaction(pTx);
    }

    BaseTransaction::Ptr Wallet::ConstructTransaction(const TxID& id, TxType type)
    {
        auto it = m_TxCreators.find(type);
        if (it == m_TxCreators.end())
        {
            LOG_WARNING() << id << " Unsupported type of transaction: " << static_cast<int>(type);
            return wallet::BaseTransaction::Ptr();
        }

        return it->second->Create(BaseTransaction::TxContext(*this, m_WalletDB, id));
    }

    BaseTransaction::Ptr Wallet::ConstructTransactionFromParameters(const TxParameters& parameters)
    {
        auto type = parameters.GetParameter<TxType>(TxParameterID::TransactionType);
        if (!type)
        {
            return BaseTransaction::Ptr();
        }

        auto it = m_TxCreators.find(*type);
        if (it == m_TxCreators.end())
        {
            LOG_ERROR() << *parameters.GetTxID() << " Unsupported type of transaction: " << static_cast<int>(*type);
            return BaseTransaction::Ptr();
        }

        auto completedParameters = it->second->CheckAndCompleteParameters(parameters);

        if (auto peerID = parameters.GetParameter(TxParameterID::PeerWalletIdentity); peerID)
        {
            auto myID = parameters.GetParameter<WalletID>(TxParameterID::MyID);
            if (myID)
            {
                auto address = m_WalletDB->getAddress(*myID);
                if (address)
                {
                    completedParameters.SetParameter(TxParameterID::MyWalletIdentity, address->m_Identity);
                }
            }
        }

        auto newTx = it->second->Create(BaseTransaction::TxContext(*this, m_WalletDB, *parameters.GetTxID()));
        ApplyTransactionParameters(newTx, completedParameters.Pack(), true);
        return newTx;
    }

    void Wallet::MakeTransactionActive(BaseTransaction::Ptr tx)
    {
        m_ActiveTransactions.emplace(tx->GetTxID(), tx);
    }

    void Wallet::ProcessStoredMessages()
    {
        if (m_MessageEndpoints.empty() || m_StoredMessagesProcessed)
        {
            return;
        }
        auto messages = m_WalletDB->getWalletMessages();
        for (auto& message : messages)
        {
            for (auto& endpoint : m_MessageEndpoints)
            {
                endpoint->SendRawMessage(message.m_PeerID, message.m_Message);
            }
            m_WalletDB->deleteWalletMessage(message.m_ID);
        }
        m_StoredMessagesProcessed = true;
    }

    bool Wallet::IsNodeInSync() const
    {
        if (m_NodeEndpoint)
        {
            Block::SystemState::Full sTip;
            get_tip(sTip);
            return IsValidTimeStamp(sTip.m_TimeStamp);
        }
        return true; // to allow made air-gapped transactions
    }

    void Wallet::RequestStateSummary()
    {
        MyRequestStateSummary::Ptr pReq(new MyRequestStateSummary);
        PostReqUnique(*pReq);
    }

    void Wallet::RestoreTransactionFromShieldedCoin(ShieldedCoin& coin)
    {
        // add virtual transaction for receiver
        beam::Block::SystemState::Full tip;
        m_WalletDB->get_History().get_Tip(tip);
        storage::DeduceStatus(*m_WalletDB, coin, tip.m_Height);

        if (coin.m_Status != ShieldedCoin::Status::Available && coin.m_Status != ShieldedCoin::Status::Spent)
        {
            return;
        }
        const auto* message = ShieldedTxo::User::ToPackedMessage(coin.m_CoinID.m_User);
        TxID txID;
        std::copy_n(message->m_TxID.m_pData, 16, txID.begin());
        auto tx = m_WalletDB->getTx(txID);
        if (tx)
        {
            return;
        }
        else
        {
            WalletAddress tempAddress;
            m_WalletDB->createAddress(tempAddress);

            auto params = CreateTransactionParameters(TxType::PushTransaction, txID)
                .SetParameter(TxParameterID::MyID, tempAddress.m_walletID)
                .SetParameter(TxParameterID::PeerID, WalletID())
                .SetParameter(TxParameterID::Status, TxStatus::Completed)
                .SetParameter(TxParameterID::Amount, coin.m_CoinID.m_Value)
                .SetParameter(TxParameterID::IsSender, false)
                .SetParameter(TxParameterID::CreateTime, RestoreCreationTime(tip, coin.m_confirmHeight))
                .SetParameter(TxParameterID::PeerWalletIdentity, coin.m_CoinID.m_User.m_Sender)
                .SetParameter(TxParameterID::MyWalletIdentity, tempAddress.m_Identity)
                .SetParameter(TxParameterID::KernelID, Merkle::Hash(Zero));

            if (message->m_maxPrivacyMinAnonimitySet)
                params.SetParameter(TxParameterID::MaxPrivacyMinAnonimitySet, message->m_maxPrivacyMinAnonimitySet);

            auto packed = params.Pack();
            for (const auto& p : packed)
            {
                storage::setTxParameter(*m_WalletDB, *params.GetTxID(), p.first, p.second, true);
            }
        }
    }
}
