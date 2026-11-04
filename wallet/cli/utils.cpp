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

#include "utils.h"

#include "wallet/core/common_utils.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"

#include <boost/format.hpp>
#include <boost/serialization/nvp.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

#include <cctype>
#include <set>

using namespace std;
using namespace beam;
using namespace beam::wallet;
using namespace ECC;

namespace beam::wallet
{
    bool ParseAmount(const std::string& strAmount, Amount& amount, std::string& error, const Amount& limit, Asset::ID assetId)
    {
        try
        {
            boost::multiprecision::cpp_dec_float_50 preciseAmount(strAmount.c_str());
            preciseAmount *= Rules::Coin;

            if (boost::multiprecision::isnan(preciseAmount))
            {
                error = "the amount ('" + strAmount + "') is invalid.";
                return false;
            }

            if (preciseAmount == 0)
            {
                error = kErrorZeroAmount;
                return false;
            }

            if (preciseAmount < 0)
            {
                error = (boost::format(kErrorNegativeAmount) % strAmount).str();
                return false;
            }

            boost::multiprecision::cpp_dec_float_50 preciseLimit(limit);
            if (preciseAmount > preciseLimit)
            {
                std::stringstream ssLimit;
                ssLimit << PrintableAmount(limit, true, assetId);
                error = (boost::format(kErrorTooBigAmount) % strAmount % ssLimit.str()).str();
                return false;
            }

            amount = preciseAmount.convert_to<Amount>();
            if (amount == 0)
            {
                // less than 1 groth
                error = kErrorZeroAmount;
                return false;
            }
        }
        catch (const std::runtime_error& err)
        {
            error = "the amount ('" + strAmount + "') is invalid. " + err.what();
            return false;
        }

        return true;
    }

    bool ReadAmount(const po::variables_map& vm, Amount& amount, const Amount& limit, Asset::ID assetId)
    {
        if (vm.count(cli::AMOUNT) == 0)
        {
            BEAM_LOG_ERROR() << kErrorAmountMissing;
            return false;
        }

        const auto strAmount = vm[cli::AMOUNT].as<std::string>();

        std::string error;
        if (!ParseAmount(strAmount, amount, error, limit, assetId))
        {
            BEAM_LOG_ERROR() << error;
            return false;
        }

        return true;
    }

    bool ParseReceiversList(const std::string& str, std::vector<ReceiverAmount>& receivers, std::string& error)
    {
        receivers.clear();

        std::vector<std::string> entries;
        std::string entry;
        bool comment = false;
        for (char c : str + "\n")
        {
            if (comment)
            {
                comment = (c != '\n');
                continue;
            }

            if (c == '#' || c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c)))
            {
                if (!entry.empty())
                    entries.push_back(std::move(entry));
                entry.clear();
                comment = (c == '#');
                continue;
            }

            entry.push_back(c);
        }

        if (entries.empty())
        {
            error = "no receivers given, expected a list of <address>:<amount> entries";
            return false;
        }

        std::set<std::string> addresses;
        for (const auto& e : entries)
        {
            auto pos = e.rfind(':');
            if (pos == std::string::npos || pos == 0 || pos + 1 == e.size())
            {
                error = "invalid receiver entry '" + e + "', expected <address>:<amount>";
                return false;
            }

            ReceiverAmount r;
            r.m_Address = e.substr(0, pos);

            std::string amountError;
            if (!ParseAmount(e.substr(pos + 1), r.m_Amount, amountError))
            {
                error = "invalid amount for receiver '" + r.m_Address + "': " + amountError;
                return false;
            }

            if (!addresses.insert(r.m_Address).second)
            {
                error = "duplicate receiver '" + r.m_Address + "'";
                return false;
            }

            receivers.push_back(std::move(r));
        }

        return true;
    }

    Amount get_MinFee(const Wallet& wallet, bool hasShieldedOutputs)
    {
        auto& fs = Transaction::FeeSettings::get(wallet.get_TipHeight());
        return hasShieldedOutputs ? fs.get_DefaultShieldedOut() : fs.get_DefaultStd();
    }

    bool ReadFee(const po::variables_map& vm, Amount& fee, const Wallet& wallet, bool hasShieldedOutputs /*= false*/)
    {
        if (auto it = vm.find(cli::FEE); it != vm.end())
        {
            fee = it->second.as<Positive<Amount>>().value;
            auto minFee = get_MinFee(wallet, hasShieldedOutputs);
            if (fee < minFee)
            {
                BEAM_LOG_ERROR() << (boost::format(kErrorFeeToLow) % minFee).str();
                return false;
            }
        }
        else
        {
            fee = get_MinFee(wallet, hasShieldedOutputs);
        }

        return true;
    }

    bool LoadReceiverParams(const po::variables_map& vm, TxParameters& params)
    {
        if (vm.find(cli::RECEIVER_ADDR) == vm.end())
        {
            BEAM_LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }

        return LoadReceiverParams(vm[cli::RECEIVER_ADDR].as<string>(), vm[cli::SEND_OFFLINE].as<bool>(), params);
    }

    bool LoadReceiverParams(const std::string& addressOrToken, bool sendOffline, TxParameters& params)
    {
        auto receiverParams = ParseParameters(addressOrToken);

        if (!receiverParams)
        {
            BEAM_LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }

        auto type = GetAddressType(addressOrToken);

        switch(type)
        {
        case TxAddressType::PublicOffline:
        case TxAddressType::MaxPrivacy:
        case TxAddressType::Regular:
            break;
        case TxAddressType::Offline:
            if (!sendOffline)
            {
                // Since v6.0 by default offline address triggers the regular online transaction
                // To execute and offline payment the --offline switch should be specified
                type = TxAddressType::Regular;
            }
            break;
        default:
            BEAM_LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }

        if (!LoadReceiverParams(*receiverParams, params, type))
        {
            return false;
        }

        if (auto peerID = params.GetParameter<WalletID>(beam::wallet::TxParameterID::PeerAddr); !peerID || std::to_string(*peerID) != addressOrToken)
        {
            params.SetParameter(beam::wallet::TxParameterID::OriginalToken, addressOrToken);
        }

        return true;
    }

    bool LoadBaseParamsForTX(const po::variables_map& vm, const Wallet& wallet, Asset::ID& assetId, Amount& amount, Amount& fee, WalletID& receiverWalletID, bool skipReceiverWalletID)
    {
        bool hasShieldedOutputs = false;
        if (!skipReceiverWalletID)
        {
            TxParameters params;
            if (!LoadReceiverParams(vm, params))
            {
                return false;
            }
            if (auto peerID = params.GetParameter<WalletID>(TxParameterID::PeerAddr); peerID)
            {
                receiverWalletID = *peerID;
            }
            if (auto txType = params.GetParameter<TxType>(TxParameterID::TransactionType))
            {
                hasShieldedOutputs = *txType == TxType::PushTransaction;
            }
        }

        if (!ReadAmount(vm, amount))
        {
            return false;
        }

        if (!ReadFee(vm, fee, wallet, hasShieldedOutputs))
        {
            return false;
        }

        if(vm.count(cli::ASSET_ID)) // asset id can be zero if beam only
        {
            assetId = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
        }

        return true;
    }
} // namespace beam::wallet