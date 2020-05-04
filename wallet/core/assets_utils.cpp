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
#include "assets_utils.h"
#include "wallet/core/common.h"
#include "utility/logger.h"
#include <regex>
#include <set>

namespace beam::wallet {
    namespace
    {
        const char STD_META_MARK[]     = "STD:";
        const char NAME_KEY[]          = "N";
        const char SHORT_NAME_KEY[]    = "SN";
        const char UNIT_NAME_KEY[]     = "UN";
        const char NTH_UNIT_NAME_KEY[] = "NTHUN";
    }

    WalletAssetMeta::WalletAssetMeta(std::string meta)
        : _std(false)
        , _meta(std::move(meta))
    {
        Parse();
    }

    WalletAssetMeta::WalletAssetMeta(const Asset::Full& info)
        : _std(false)
    {
        const auto& mval = info.m_Metadata.m_Value;
        if (mval.empty())
        {
            return;
        }

        if(!fromByteBuffer(mval, _meta))
        {
            LOG_WARNING() << "AssetID " << info.m_ID << " failed to deserialize from Asset::Full";
            return;
        }

        Parse();
    }

    void WalletAssetMeta::Parse()
    {
        _std = false;

        const auto STD_LEN = std::size(STD_META_MARK) - 1;
        if(strncmp(_meta.c_str(), STD_META_MARK, STD_LEN) != 0) return;

        std::regex rg{R"([^;]+)"};
        std::set<std::string> tokens{
            std::sregex_token_iterator{_meta.begin() + STD_LEN, _meta.end(), rg},
            std::sregex_token_iterator{}
        };

        for(const auto& it: tokens)
        {
            auto eq = it.find_first_of('=');
            if (eq == std::string::npos) continue;
            auto key = std::string(it.begin(), it.begin() + eq);
            auto val = std::string(it.begin() + eq + 1, it.end());
            _values[key] = val;
        }

        _std = _values.find(NAME_KEY) != _values.end() &&
               _values.find(SHORT_NAME_KEY) != _values.end() &&
               _values.find(UNIT_NAME_KEY) != _values.end() &&
               _values.find(NTH_UNIT_NAME_KEY) != _values.end();
    }

    void WalletAssetMeta::LogInfo(const std::string& prefix) const
    {
        for(const auto& it: _values)
        {
            LOG_INFO() << prefix << it.first << "=" << it.second;
        }
    }

    bool WalletAssetMeta::isStd() const
    {
        return _std;
    }

    std::string WalletAssetMeta::GetUnitName() const
    {
        const auto it = _values.find(UNIT_NAME_KEY);
        return it != _values.end() ? it->second : std::string();
    }

    std::string WalletAssetMeta::GetNthUnitName() const
    {
        const auto it = _values.find(NTH_UNIT_NAME_KEY);
        return it != _values.end() ? it->second : std::string();
    }

    std::string WalletAssetMeta::GetName() const
    {
        const auto it = _values.find(NAME_KEY);
        return it != _values.end() ? it->second : std::string();
    }

    std::string WalletAssetMeta::GetShortName() const
    {
        const auto it = _values.find(SHORT_NAME_KEY);
        return it != _values.end() ? it->second : std::string();
    }
}
