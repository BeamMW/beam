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

#pragma once

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

struct IWalletModelAsync
{
    using Ptr = std::shared_ptr<IWalletModelAsync>;

    virtual void sendMoney(const beam::WalletID& receiver, const std::string& comment, beam::Amount&& amount, beam::Amount&& fee = 0) = 0;
    virtual void syncWithNode() = 0;
    virtual void calcChange(beam::Amount&& amount) = 0;
    virtual void getWalletStatus() = 0;
    virtual void getUtxosStatus() = 0;
    virtual void getAddresses(bool own) = 0;
    virtual void cancelTx(const beam::TxID& id) = 0;
    virtual void deleteTx(const beam::TxID& id) = 0;
    virtual void saveAddress(const beam::WalletAddress& address, bool bOwn) = 0;
    virtual void generateNewAddress() = 0;
    virtual void changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID) = 0;

    virtual void deleteAddress(const beam::WalletID& id) = 0;

    virtual void setNodeAddress(const std::string& addr) = 0;

    virtual void changeWalletPassword(const beam::SecString& password) = 0;

    virtual ~IWalletModelAsync() {}
};
