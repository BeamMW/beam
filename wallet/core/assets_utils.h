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

#include <core/block_crypt.h>
#include "common.h"

namespace beam::wallet {
    class WalletAssetMeta
    {
    public:
        explicit WalletAssetMeta(std::string meta);
        explicit WalletAssetMeta(const Asset::Full& info);

        bool isStd() const;
        bool isStd_v5_0() const;
        void LogInfo(const std::string& prefix = "\t") const;

        std::string GetUnitName() const;
        std::string GetNthUnitName() const;
        std::string GetName() const;
        std::string GetShortName() const;
        std::string GetShortDesc() const;
        std::string GetLongDesc() const;
        std::string GetSiteUrl() const;
        std::string GetPaperUrl() const;
        std::string GetColor() const;
        unsigned GetSchemaVersion() const;

        typedef std::map<std::string, std::string> MetaMap;
        inline const MetaMap& GetMetaMap() const
        {
            return _values;
        }

    private:
        void Parse();

        MetaMap _values;
        bool _std;
        bool _std_v5_0;
        std::string _meta;
    };

    struct IWalletDB;
    class WalletAsset: public Asset::Full
    {
    public:
        WalletAsset() = default;
        WalletAsset(const Asset::Full& full, Height refreshHeight);
        ~WalletAsset() = default;

        bool CanRollback(Height from) const;
        bool IsExpired(IWalletDB& wdb);
        void LogInfo(const std::string& prefix = std::string()) const;
        void LogInfo(const TxID& txId, const SubTxID& subTxId) const;

        Height  m_RefreshHeight = 0;
        int32_t m_IsOwned = 0;
    };

    PeerID GetAssetOwnerID(const Key::IKdf::Ptr& masterKdf, const std::string& meta);
}
