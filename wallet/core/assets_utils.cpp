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
#include "wallet/core/strings_resources.h"
#include "utility/logger.h"
#include "wallet_db.h"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <regex>
#include <set>
#include <sstream>

namespace beam::wallet {
    namespace
    {
        const char STD_META_MARK[]     = "STD:";
        const char VERSION_KEY[]       = "SCH_VER";
        const char NAME_KEY[]          = "N";
        const char SHORT_NAME_KEY[]    = "SN";
        const char UNIT_NAME_KEY[]     = "UN";
        const char NTH_UNIT_NAME_KEY[] = "NTHUN";
        const char OPT_SDESC_KEY[]     = "OPT_SHORT_DESC";
        const char OPT_LDESC_KEY[]     = "OPT_LONG_DESC";
        const char OPT_SITE_URL[]      = "OPT_SITE_URL";
        const char OPT_PAPER_URL[]     = "OPT_PDF_URL";
        const char OPT_COLOR[]         = "OPT_COLOR";
        const char ALLOWED_SYMBOLS[]   = " .,-_";
        const unsigned CURRENT_META_VERSION = 1;
        const size_t MAX_SHORT_NAME_LEN = 6;
        const size_t MAX_SHORT_DESC_LEN = 128;
        const size_t MAX_LONG_DESC_LEN  = 1024;
    }

    WalletAssetMeta::WalletAssetMeta(std::string meta)
        : _std(false)
        , _std_v5_0(false)
        , _meta(std::move(meta))
    {
        Parse();
    }

    WalletAssetMeta::WalletAssetMeta(const Asset::Full& info)
        : _std(false)
        , _std_v5_0(false)
    {
        info.m_Metadata.get_String(_meta);
        if (_meta.empty())
        {
            _parseError = "metadata is empty";
            return;
        }

        try {
            Parse();
        }
        catch (const std::exception& exc) {
            _parseError = std::string("failed to parse metadata: ") + exc.what();
            BEAM_LOG_WARNING() << "AssetID " << info.m_ID << " failed to deserialize metadata: " << exc.what();
        }
    }

    void WalletAssetMeta::Parse()
    {
        _std = false;
        _parseError.clear();

        // Remembers the first problem found and always returns false,
        // so it can be used directly inside the validation expressions below
        const auto fail = [this](const std::string& reason) -> bool {
            if (_parseError.empty())
            {
                _parseError = reason;
            }
            return false;
        };

        const auto STD_LEN = std::size(STD_META_MARK) - 1;
        if(strncmp(_meta.c_str(), STD_META_MARK, STD_LEN) != 0)
        {
            fail(std::string("metadata must start with \"") + STD_META_MARK + "\"");
            return;
        }

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

        const auto fieldTitle = [](const char* name, const char* descr) -> std::string {
            return std::string(name) + " (" + descr + ")";
        };

        const auto printChar = [](const char ch) -> std::string {
            std::stringstream ss;
            if (std::isprint(ch, std::locale::classic()))
            {
                ss << "'" << ch << "'";
            }
            else
            {
                ss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(static_cast<unsigned char>(ch));
            }
            return ss.str();
        };

        // Quotes a user supplied value for an error message. Non-printable values are not echoed back
        // and long values are truncated, so the message stays readable and safe to put into JSON.
        const auto printValue = [](const std::string& value) -> std::string {
            const size_t MAX_ECHO_LEN = 32;
            const bool printable = std::all_of(value.begin(), value.end(), [](const char ch) -> bool {
                return std::isprint(ch, std::locale::classic());
            });

            if (!printable) return "(non-printable value)";
            if (value.length() <= MAX_ECHO_LEN) return "\"" + value + "\"";
            return "\"" + value.substr(0, MAX_ECHO_LEN) + "...\"";
        };

        const auto fieldValid = [&](const char* name, const char* descr) -> bool {
            const auto it = _values.find(name);
            if (it == _values.end())
            {
                return fail("required field " + fieldTitle(name, descr) + " is missing");
            }

            const auto bad = std::find_if_not(it->second.begin(), it->second.end(), [](const char ch) -> bool {
                return std::isalnum(ch, std::locale::classic()) || std::string(ALLOWED_SYMBOLS).find(ch) != std::string::npos;
            });

            if (bad != it->second.end())
            {
                return fail(fieldTitle(name, descr) + " contains invalid character " + printChar(*bad) +
                            ", only letters, digits, spaces and \"" + std::string(ALLOWED_SYMBOLS + 1) + "\" are allowed");
            }

            return true;
        };

        _std_v5_0 =
               fieldValid(NAME_KEY, "asset name") &&
               fieldValid(SHORT_NAME_KEY, "short name") &&
               fieldValid(UNIT_NAME_KEY, "unit name") &&
               fieldValid(NTH_UNIT_NAME_KEY, "smallest unit name");

        const auto versionValid = [&] () -> bool {
            const auto it = _values.find(VERSION_KEY);
            if (it == _values.end())
            {
                return fail(std::string("required field ") + VERSION_KEY + " (schema version) is missing");
            }

            const auto version = std::to_unsigned(it->second, false);
            if (version != CURRENT_META_VERSION)
            {
                return fail(std::string("unsupported ") + VERSION_KEY + " (schema version) " + printValue(it->second) +
                            ", must be " + std::to_string(CURRENT_META_VERSION));
            }

            return true;
        };

        const auto optLenValid = [&] (const char* name, size_t maxLen) -> bool {
            const auto it = _values.find(name);
            if (it == _values.end()) return true;
            if (it->second.length() <= maxLen) return true;

            return fail(std::string(name) + " is too long: " + std::to_string(it->second.length()) +
                        " characters, maximum is " + std::to_string(maxLen));
        };

        const auto optColorValid = [&] () -> bool {
            const auto it = _values.find(OPT_COLOR);
            if (it == _values.end()) return true;

            std::regex rg{R"(^#(?:[0-9a-fA-F]{3}){1,2}$)"};
            if (std::regex_match(it->second, rg)) return true;

            return fail(std::string(OPT_COLOR) + " " + printValue(it->second) + " is invalid, must be a hex color like #RGB or #RRGGBB");
        };

        _std_v6_0 = _std_v5_0 &&
                versionValid() &&
                optLenValid(OPT_SDESC_KEY, MAX_SHORT_DESC_LEN) &&
                optLenValid(OPT_LDESC_KEY, MAX_LONG_DESC_LEN) &&
                optColorValid();

        const auto notEmpty = [&](const std::string& value, const char* name, const char* descr) -> bool {
            return !value.empty() || fail(fieldTitle(name, descr) + " must not be empty");
        };

        const auto shortNameLenValid = [&]() -> bool {
            const auto len = GetShortName().length();
            if (len <= MAX_SHORT_NAME_LEN) return true;

            return fail(fieldTitle(SHORT_NAME_KEY, "short name") + " is too long: " + std::to_string(len) +
                        " characters, maximum is " + std::to_string(MAX_SHORT_NAME_LEN));
        };

        _std = _std_v6_0 &&
                notEmpty(GetName(), NAME_KEY, "asset name") &&
                notEmpty(GetShortName(), SHORT_NAME_KEY, "short name") && shortNameLenValid() &&
                notEmpty(GetUnitName(), UNIT_NAME_KEY, "unit name") &&
                notEmpty(GetNthUnitName(), NTH_UNIT_NAME_KEY, "smallest unit name");
    }

    void WalletAssetMeta::LogInfo(const std::string& pref) const
    {
        const auto prefix = pref.empty() ? pref : pref + " ";
        const auto isPrintable = [](const std::string& str) -> bool {
            std::locale loc("C");
            return std::all_of(str.begin(), str.end(), [&loc](const char ch) -> bool {
                return std::isprint(ch, loc);
            });
        };

        for(const auto& it: _values)
        {
            std::string value = it.second;
            if (!isPrintable(value))
            {
                std::stringstream  ss;
                ss << "[CANNOT BE PRINTED, size is " << value.size() << " bytes]";
                value = ss.str();
            }
            BEAM_LOG_INFO() << prefix << it.first << "=" << value;
        }
    }

    bool WalletAssetMeta::isStd() const
    {
        return _std;
    }

    bool WalletAssetMeta::isStd_v5_0() const
    {
        return _std_v5_0;
    }

    bool WalletAssetMeta::isStd_v6_0() const
    {
        return _std_v6_0;
    }

    const std::string& WalletAssetMeta::GetParseError() const
    {
        return _parseError;
    }

    std::string WalletAssetMeta::GetUnitName() const
    {
        const auto it = _values.find(UNIT_NAME_KEY);
        return it != _values.end() ? it->second : std::string(kAmountASSET);
    }

    std::string WalletAssetMeta::GetNthUnitName() const
    {
        const auto it = _values.find(NTH_UNIT_NAME_KEY);
        return it != _values.end() ? it->second : std::string(kAmountAGROTH);
    }

    std::string WalletAssetMeta::GetShortDesc() const
    {
        const auto it = _values.find(OPT_SDESC_KEY);
        return it != _values.end() ? it->second : std::string();
    }

    std::string WalletAssetMeta::GetLongDesc() const
    {
        const auto it = _values.find(OPT_LDESC_KEY);
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

    std::string WalletAssetMeta::GetSiteUrl() const
    {
        const auto it = _values.find(OPT_SITE_URL);
        return it != _values.end() ? it->second : std::string();
    }

    std::string WalletAssetMeta::GetPaperUrl() const
    {
        const auto it = _values.find(OPT_PAPER_URL);
        return it != _values.end() ? it->second : std::string();
    }

    std::string WalletAssetMeta::GetColor() const
    {
        const auto it = _values.find(OPT_COLOR);
        return it != _values.end() ? it->second : std::string();
    }

    unsigned WalletAssetMeta::GetSchemaVersion() const
    {
        const auto it = _values.find(SHORT_NAME_KEY);
        return it != _values.end() ? std::to_unsigned(it->second, false) : 0;
    }

    WalletAsset::WalletAsset(const Asset::Full& full, Height refreshHeight)
        : Asset::Full(full)
        , m_RefreshHeight(refreshHeight)
    {
    }

    bool WalletAsset::CanRollback(Height from) const
    {
        const auto maxRollback = Rules::get().MaxRollback;
        return m_LockHeight + maxRollback > from;
    }

    bool WalletAsset::IsExpired(IWalletDB& wdb)
    {
        const auto getRange = [](const WalletAsset& info) -> auto {
            HeightRange result;
            result.m_Min = info.m_LockHeight;
            result.m_Max = AmountBig::get_Lo(info.m_LockHeight) > 0 ? info.m_RefreshHeight : info.m_LockHeight;
            result.m_Max += Rules::get().CA.LockPeriod;
            return result;
        };

        const auto currHeight = wdb.getCurrentHeight();
        HeightRange hrange = getRange(*this);
        if (m_Value != Zero)
        {
            wdb.visitCoins([&](const Coin& coin) -> bool {
                if (coin.m_ID.m_AssetID != m_ID) return true;
                if (coin.m_confirmHeight > hrange.m_Max) return true;
                if (coin.m_confirmHeight < hrange.m_Min) return true;

                const Height h1 = coin.m_spentHeight != MaxHeight ? coin.m_spentHeight : currHeight;
                if (m_RefreshHeight < h1)
                {
                    m_RefreshHeight = h1;
                    hrange = getRange(*this);
                }
                return true;
            });

            wdb.visitShieldedCoins([&](const ShieldedCoin& coin) -> bool {
                if (coin.m_CoinID.m_AssetID != m_ID) return true;
                if (coin.m_confirmHeight > hrange.m_Max) return true;
                if (coin.m_confirmHeight < hrange.m_Min) return true;

                const Height h1 = coin.m_spentHeight != MaxHeight ? coin.m_spentHeight : currHeight;
                if (m_RefreshHeight < h1)
                {
                    m_RefreshHeight = h1;
                    hrange = getRange(*this);
                }
                return true;
            });
        }

        return !hrange.IsInRange(currHeight) || hrange.m_Max < currHeight;
    }

    void WalletAsset::LogInfo(const std::string& pref) const
    {
        const auto prefix = pref.empty() ? pref : pref + " ";

        BEAM_LOG_INFO() << prefix << "Asset ID: "       << m_ID;
        BEAM_LOG_INFO() << prefix << "Owner ID: "       << m_Owner;
        BEAM_LOG_INFO() << prefix << "Issued amount: "  << PrintableAmount(m_Value);
        BEAM_LOG_INFO() << prefix << "Lock Height: "    << m_LockHeight;
        BEAM_LOG_INFO() << prefix << "Refresh height: " << m_RefreshHeight;
        BEAM_LOG_INFO() << prefix << "Metadata size: "  << m_Metadata.m_Value.size() << " bytes";

        const WalletAssetMeta meta(*this);
        meta.LogInfo(pref + "\t");

        if(m_IsOwned)
        {
            BEAM_LOG_INFO() << prefix << "You own this asset";
        }
    }

    void WalletAsset::LogInfo(const TxID& txId, const SubTxID& subTxId) const
    {
        std::stringstream ss;
        ss << txId << "[" << subTxId << "]";
        const auto prefix = ss.str();
        LogInfo(prefix);
    }

    PeerID GetAssetOwnerID(const Key::IKdf::Ptr& masterKdf, const std::string& strMeta)
    {
        Asset::Metadata meta;
        meta.set_String(strMeta, true);
        meta.UpdateHash();

        PeerID ownerID = 0UL;
        meta.get_Owner(ownerID, *masterKdf);

        return ownerID;
    }
}
