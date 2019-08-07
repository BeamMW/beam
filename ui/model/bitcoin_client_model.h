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

#pragma once

#include <QObject>
#include "wallet/bitcoin/bitcoin_client.h"

class BitcoinClientModel
    : public QObject
    , public beam::BitcoinClient
{
    Q_OBJECT
public:
    using Ptr = std::shared_ptr<BitcoinClientModel>;

    BitcoinClientModel(beam::wallet::IWalletDB::Ptr walletDB, beam::io::Reactor& reactor);

signals:
    void GotStatus(beam::BitcoinClient::Status status);
    void GotBalance(const beam::BitcoinClient::Balance& balance);

private:
    void OnStatus(Status status) override;
    void OnBalance(const BitcoinClient::Balance& balance) override;
};