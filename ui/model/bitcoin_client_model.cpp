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

#include "bitcoin_client_model.h"


using namespace beam;


BitcoinClientModel::BitcoinClientModel(wallet::IWalletDB::Ptr walletDB, io::Reactor& reactor)
    : BitcoinClient(walletDB, reactor)
{
    qRegisterMetaType<beam::BitcoinClient::Status>("beam::BitcoinClient::Status");
    qRegisterMetaType<beam::BitcoinClient::Balance>("beam::BitcoinClient::Balance");
}

void BitcoinClientModel::OnStatus(Status status)
{
    emit GotStatus(status);
}

void BitcoinClientModel::OnBalance(const BitcoinClient::Balance& balance)
{
    emit GotBalance(balance);
}
