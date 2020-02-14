// Copyright 2018-2020 The Beam Team
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

#include "api_connection.h"

#include "wallet/core/simple_transaction.h"

using namespace beam;

namespace
{
    std::string getMinimumFeeError(Amount minimumFee)
    {
        std::stringstream ss;
        ss << "Failed to initiate the send operation. The minimum fee is " << minimumFee << " GROTH.";
        return ss.str();
    }
}

namespace beam::wallet
{
    ApiConnection::ApiConnection(IWalletData& walletData, WalletApi::ACL acl)
        : _walletData(walletData)
        , _api(*this, acl)
    {
    }

    ApiConnection::~ApiConnection()
    {
    }

    void ApiConnection::doError(const JsonRpcId& id, ApiError code, const std::string& data)
    {
        json msg
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error",
                {
                    {"code", code},
                    {"message", WalletApi::getErrorMessage(code)}
                }
            }
        };

        if (!data.empty())
        {
            msg["error"]["data"] = data;
        }

        serializeMsg(msg);
    }

    void ApiConnection::onInvalidJsonRpc(const json& msg)
    {
        LOG_DEBUG() << "onInvalidJsonRpc: " << msg;

        serializeMsg(msg);
    }

    void ApiConnection::FillAddressData(const AddressData& data, WalletAddress& address)
    {
        if (data.comment)
        {
            address.setLabel(*data.comment);
        }

        if (data.expiration)
        {
            switch (*data.expiration)
            {
            case EditAddress::OneDay:
                address.setExpiration(WalletAddress::ExpirationStatus::OneDay);
                break;
            case EditAddress::Expired:
                address.setExpiration(WalletAddress::ExpirationStatus::Expired);
                break;
            case EditAddress::Never:
                address.setExpiration(WalletAddress::ExpirationStatus::Never);
                break;
            }
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const CreateAddress& data)
    {
        LOG_DEBUG() << "CreateAddress(id = " << id << ")";

        WalletAddress address;
        _walletData.getWalletDB()->createAddress(address);
        FillAddressData(data, address);

        _walletData.getWalletDB()->saveAddress(address);

        doResponse(id, CreateAddress::Response{ address.m_walletID });
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const DeleteAddress& data)
    {
        LOG_DEBUG() << "DeleteAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

        auto addr = _walletData.getWalletDB()->getAddress(data.address);

        if (addr)
        {
            _walletData.getWalletDB()->deleteAddress(data.address);

            doResponse(id, DeleteAddress::Response{});
        }
        else
        {
            doError(id, ApiError::InvalidAddress, "Provided address doesn't exist.");
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const EditAddress& data)
    {
        LOG_DEBUG() << "EditAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

        auto addr = _walletData.getWalletDB()->getAddress(data.address);

        if (addr)
        {
            if (addr->isOwn())
            {
                FillAddressData(data, *addr);
                _walletData.getWalletDB()->saveAddress(*addr);

                doResponse(id, EditAddress::Response{});
            }
            else
            {
                doError(id, ApiError::InvalidAddress, "You can edit only own address.");
            }
        }
        else
        {
            doError(id, ApiError::InvalidAddress, "Provided address doesn't exist.");
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const AddrList& data)
    {
        LOG_DEBUG() << "AddrList(id = " << id << ")";

        doResponse(id, AddrList::Response{ _walletData.getWalletDB()->getAddresses(data.own) });
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const ValidateAddress& data)
    {
        LOG_DEBUG() << "ValidateAddress( address = " << std::to_string(data.address) << ")";

        auto addr = _walletData.getWalletDB()->getAddress(data.address);
        bool isMine = addr ? addr->isOwn() : false;
        doResponse(id, ValidateAddress::Response{ data.address.IsValid() && (isMine ? !addr->isExpired() : true), isMine });
    }

    void ApiConnection::doTxAlreadyExistsError(const JsonRpcId& id)
    {
        doError(id, ApiError::InvalidTxId, "Provided transaction ID already exists in the wallet.");
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const Send& data)
    {
        LOG_DEBUG() << "Send(id = " << id << " amount = " << data.value << " fee = " << data.fee << " address = " << std::to_string(data.address) << ")";

        try
        {
            WalletID from(Zero);

            if (data.from)
            {
                if (!data.from->IsValid())
                {
                    doError(id, ApiError::InvalidAddress, "Invalid sender address.");
                    return;
                }

                auto addr = _walletData.getWalletDB()->getAddress(*data.from);
                bool isMine = addr ? addr->isOwn() : false;

                if (!isMine)
                {
                    doError(id, ApiError::InvalidAddress, "It's not your own address.");
                    return;
                }

                if (addr->isExpired())
                {
                    doError(id, ApiError::InvalidAddress, "Sender address is expired.");
                    return;
                }

                from = *data.from;
            }
            else
            {
                WalletAddress senderAddress;
                _walletData.getWalletDB()->createAddress(senderAddress);
                _walletData.getWalletDB()->saveAddress(senderAddress);

                from = senderAddress.m_walletID;
            }

            ByteBuffer message(data.comment.begin(), data.comment.end());

            CoinIDList coins;

            if (data.session)
            {
                coins = _walletData.getWalletDB()->getLockedCoins(*data.session);

                if (coins.empty())
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Requested session is empty.");
                    return;
                }
            }
            else
            {
                coins = data.coins ? *data.coins : CoinIDList();
            }

            auto minimumFee = std::max(wallet::GetMinimumFee(2), DefaultFee); // receivers's output + change
            if (data.fee < minimumFee)
            {
                doError(id, ApiError::InternalErrorJsonRpc, getMinimumFeeError(minimumFee));
                return;
            }

            if (data.txId && _walletData.getWalletDB()->getTx(*data.txId))
            {
                doTxAlreadyExistsError(id);
                return;
            }

            auto params = CreateSimpleTransactionParameters(data.txId);
            LoadReceiverParams(data.txParameters, params);

            params.SetParameter(TxParameterID::MyID, from)
                .SetParameter(TxParameterID::Amount, data.value)
                .SetParameter(TxParameterID::Fee, data.fee)
                .SetParameter(TxParameterID::PreselectedCoins, coins)
                .SetParameter(TxParameterID::Message, message);

            auto txId = _walletData.getWallet().StartTransaction(params);

            doResponse(id, Send::Response{ txId });
        }
        catch (...)
        {
            doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const Issue& data)
    {
        LOG_DEBUG() << "Issue(id = " << id << " amount = " << data.value << " fee = " << data.fee;

        try
        {
            CoinIDList coins;

            if (data.session)
            {
                if ((coins = _walletData.getWalletDB()->getLockedCoins(*data.session)).empty())
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Requested session is empty.");
                    return;
                }
            }
            else
            {
                coins = data.coins ? *data.coins : CoinIDList();
            }

            auto minimumFee = std::max(wallet::GetMinimumFee(2), DefaultFee);
            if (data.fee < minimumFee)
            {
                doError(id, ApiError::InternalErrorJsonRpc, getMinimumFeeError(minimumFee));
                return;
            }

            if (data.txId && _walletData.getWalletDB()->getTx(*data.txId))
            {
                doTxAlreadyExistsError(id);
                return;
            }

            const auto txId = _walletData.getWallet().StartTransaction(CreateTransactionParameters(TxType::AssetIssue, data.txId)
                .SetParameter(TxParameterID::Amount, data.value)
                .SetParameter(TxParameterID::Fee, data.fee)
                .SetParameter(TxParameterID::PreselectedCoins, coins)
                .SetParameter(TxParameterID::AssetOwnerIdx, data.index));

            doResponse(id, Issue::Response{ txId });
        }
        catch (...)
        {
            doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const Status& data)
    {
        LOG_DEBUG() << "Status(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto tx = _walletData.getWalletDB()->getTx(data.txId);

        if (tx)
        {
            Block::SystemState::ID stateID = {};
            _walletData.getWalletDB()->getSystemStateID(stateID);

            Status::Response result;
            result.tx = *tx;
            result.kernelProofHeight = 0;
            result.systemHeight = stateID.m_Height;
            result.confirmations = 0;

            storage::getTxParameter(*_walletData.getWalletDB(), tx->m_txId, TxParameterID::KernelProofHeight, result.kernelProofHeight);

            doResponse(id, result);
        }
        else
        {
            doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const Split& data)
    {
        LOG_DEBUG() << "Split(id = " << id << " coins = [";
        for (auto& coin : data.coins) LOG_DEBUG() << coin << ",";
        LOG_DEBUG() << "], fee = " << data.fee;
        try
        {
            WalletAddress senderAddress;
            _walletData.getWalletDB()->createAddress(senderAddress);
            _walletData.getWalletDB()->saveAddress(senderAddress);

            auto minimumFee = std::max(wallet::GetMinimumFee(data.coins.size() + 1), DefaultFee); // +1 extra output for change 
            if (data.fee < minimumFee)
            {
                doError(id, ApiError::InternalErrorJsonRpc, getMinimumFeeError(minimumFee));
                return;
            }

            if (data.txId && _walletData.getWalletDB()->getTx(*data.txId))
            {
                doTxAlreadyExistsError(id);
                return;
            }

            auto txId = _walletData.getWallet().StartTransaction(CreateSplitTransactionParameters(senderAddress.m_walletID, data.coins, data.txId)
                .SetParameter(TxParameterID::Fee, data.fee));

            doResponse(id, Send::Response{ txId });
        }
        catch (...)
        {
            doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const TxCancel& data)
    {
        LOG_DEBUG() << "TxCancel(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto tx = _walletData.getWalletDB()->getTx(data.txId);

        if (tx)
        {
            if (_walletData.getWallet().CanCancelTransaction(tx->m_txId))
            {
                _walletData.getWallet().CancelTransaction(tx->m_txId);
                TxCancel::Response result{ true };
                doResponse(id, result);
            }
            else
            {
                doError(id, ApiError::InvalidTxStatus, "Transaction could not be cancelled. Invalid transaction status.");
            }
        }
        else
        {
            doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const TxDelete& data)
    {
        LOG_DEBUG() << "TxDelete(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto tx = _walletData.getWalletDB()->getTx(data.txId);

        if (tx)
        {
            if (tx->canDelete())
            {
                _walletData.getWalletDB()->deleteTx(data.txId);

                if (_walletData.getWalletDB()->getTx(data.txId))
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Transaction not deleted.");
                }
                else
                {
                    doResponse(id, TxDelete::Response{ true });
                }
            }
            else
            {
                doError(id, ApiError::InternalErrorJsonRpc, "Transaction can't be deleted.");
            }
        }
        else
        {
            doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const GetUtxo& data)
    {
        LOG_DEBUG() << "GetUtxo(id = " << id << ")";

        GetUtxo::Response response;
        _walletData.getWalletDB()->visitCoins([&response](const Coin& c)->bool
            {
                response.utxos.push_back(c);
                return true;
            });

        doPagination(data.skip, data.count, response.utxos);

        doResponse(id, response);
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const WalletStatus& data)
    {
        LOG_DEBUG() << "WalletStatus(id = " << id << ")";

        WalletStatus::Response response;

        {
            Block::SystemState::ID stateID = {};
            _walletData.getWalletDB()->getSystemStateID(stateID);

            response.currentHeight = stateID.m_Height;
            response.currentStateHash = stateID.m_Hash;
        }

        {
            Block::SystemState::Full state;
            _walletData.getWalletDB()->get_History().get_Tip(state);
            response.prevStateHash = state.m_Prev;
            response.difficulty = state.m_PoW.m_Difficulty.ToFloat();
        }

        storage::Totals allTotals(*_walletData.getWalletDB());
        const auto& totals = allTotals.GetTotals(Zero);

        response.available = totals.Avail;
        response.receiving = totals.Incoming;
        response.sending = totals.Outgoing;
        response.maturing = totals.Maturing;

        doResponse(id, response);
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const GenerateTxId& data)
    {
        LOG_DEBUG() << "GenerateTxId(id = " << id << ")";

        doResponse(id, GenerateTxId::Response{ wallet::GenerateTxID() });
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const Lock& data)
    {
        LOG_DEBUG() << "Lock(id = " << id << ")";

        Lock::Response response;

        response.result = _walletData.getWalletDB()->lockCoins(data.coins, data.session);

        doResponse(id, response);
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const Unlock& data)
    {
        LOG_DEBUG() << "Unlock(id = " << id << " session = " << data.session << ")";

        Unlock::Response response;

        response.result = _walletData.getWalletDB()->unlockCoins(data.session);

        doResponse(id, response);
    }

    void ApiConnection::onMessage(const JsonRpcId& id, const TxList& data)
    {
        LOG_DEBUG() << "List(filter.status = " << (data.filter.status ? std::to_string((uint32_t)*data.filter.status) : "nul") << ")";

        TxList::Response res;

        {
            auto txList = _walletData.getWalletDB()->getTxHistory();

            Block::SystemState::ID stateID = {};
            _walletData.getWalletDB()->getSystemStateID(stateID);

            for (const auto& tx : txList)
            {
                Status::Response item;
                item.tx = tx;
                item.kernelProofHeight = 0;
                item.systemHeight = stateID.m_Height;
                item.confirmations = 0;

                storage::getTxParameter(*_walletData.getWalletDB(), tx.m_txId, TxParameterID::KernelProofHeight, item.kernelProofHeight);
                res.resultList.push_back(item);
            }
        }

        using Result = decltype(res.resultList);

        // filter transactions by status if provided
        if (data.filter.status)
        {
            Result filteredList;

            for (const auto& it : res.resultList)
                if (it.tx.m_status == *data.filter.status)
                    filteredList.push_back(it);

            res.resultList = filteredList;
        }

        // filter transactions by height if provided
        if (data.filter.height)
        {
            Result filteredList;

            for (const auto& it : res.resultList)
                if (it.kernelProofHeight == *data.filter.height)
                    filteredList.push_back(it);

            res.resultList = filteredList;
        }

        doPagination(data.skip, data.count, res.resultList);

        doResponse(id, res);
    }

} // beam::wallet