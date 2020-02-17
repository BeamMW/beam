// Copyright 2020 The Beam Team
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

#include "wallet/client/extensions/news_channels/interface.h"
#include "wallet/core/wallet_db.h"

namespace beam::wallet
{
    struct Notification
    {
        enum class Type : uint32_t
        {
            SoftwareUpdateAvailable,
            AddressStatusChanged,
            TransactionStatusChanged,
            BeamNews
        };

        // unique ID - probably same as BroadcastMsg::m_signature underlying type
        ECC::uintBig m_id;
        Type m_type;
        Timestamp m_ts;
        ByteBuffer m_content;
    };

    class NotificationCenter
        : public INewsObserver
    {
    public:
        NotificationCenter(IWalletDB& storage);

        std::vector<Notification> getNotifications();

        // INewsObserver implementation
        virtual void onNewWalletVersion(const VersionInfo&) override;
        virtual void onExchangeRates(const ExchangeRates&) override;

        // TODO interface for wallet transactions and addresses changes listening

    private:
        void loadToCache();
        void store(const Notification&);

        IWalletDB& m_storage;
        // std::unordered_map<ECC::uintBig, Notification> m_cache;
    };
} // namespace beam::wallet
