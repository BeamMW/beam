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

#include "wallet/core/common_utils.h"
#include "wallet/core/common.h"
#include "boost/any.hpp"

namespace beam::wallet
{
    class DexOrder;
    struct DexOrderID;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    struct SwapOffer;
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
    struct IWalletModelAsync
    {
        using Ptr = std::shared_ptr<IWalletModelAsync>;
        template<typename ...Args> using AsyncCallback = std::function<void(Args...)>;
        virtual void sendMoney(const WalletID& receiver, const std::string& comment, Amount amount, Amount fee = 0) = 0;
        virtual void sendMoney(const WalletID& sender, const WalletID& receiver, const std::string& comment, Amount amount, Amount fee = 0) = 0;
        virtual void startTransaction(TxParameters&& parameters) = 0;
        virtual void syncWithNode() = 0;
        virtual void calcChange(Amount amount, Amount fee, Asset::ID assetId) = 0;
        virtual void selectCoins(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded = false) = 0;
        virtual void selectCoins(Amount amount, Amount beforehandMinFee, Asset::ID assetId, bool isShielded, AsyncCallback<const CoinsSelectionInfo&>&&) = 0;
        virtual void getWalletStatus() = 0;
        virtual void getTransactions() = 0;
        virtual void getTransactions(AsyncCallback<const std::vector<TxDescription>&>&&) = 0;
        virtual void getAllUtxosStatus() = 0;
        virtual void cancelTx(const TxID& id) = 0;
        virtual void deleteTx(const TxID& id) = 0;
        virtual void getCoinsByTx(const TxID& txId) = 0;
        virtual void saveAddress(const WalletAddress& address) = 0;
        virtual void generateNewAddress() = 0;
        virtual void generateNewAddress(AsyncCallback<const WalletAddress&>&& callback) = 0;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        virtual void loadSwapParams() = 0;
        virtual void storeSwapParams(const beam::ByteBuffer& params) = 0;
        virtual void getSwapOffers() = 0;
        virtual void publishSwapOffer(const SwapOffer& offer) = 0;
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

        virtual void getDexOrders() = 0;
        virtual void publishDexOrder(const DexOrder&) = 0;

        // TODO:DEX this is only for test, if will remain consider replacing QString to actual type
        virtual void acceptDexOrder(const DexOrderID&) = 0;

        virtual void deleteAddress(const WalletID& addr) = 0;
        virtual void updateAddress(const WalletID& addr, const std::string& name, WalletAddress::ExpirationStatus expirationStatus) = 0;
        virtual void updateAddress(const WalletID& addr, const std::string& name, beam::Timestamp expiration) = 0;
        virtual void activateAddress(const WalletID& addr) = 0;
        virtual void getAddresses(bool own) = 0;
        virtual void getAddress(const WalletID& addr) = 0;
        virtual void getAddress(const WalletID& addr, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback) = 0;
        virtual void getAddressByToken(const std::string& token, AsyncCallback<const boost::optional<WalletAddress>&, size_t>&& callback) = 0;
        virtual void deleteAddressByToken(const std::string& addr) = 0;

        virtual void saveVouchers(const ShieldedVoucherList& v, const WalletID& walletID) = 0;
        virtual void setNodeAddress(const std::string& addr) = 0;
        virtual void changeWalletPassword(const beam::SecString& password) = 0;

        virtual void getNetworkStatus() = 0;
        virtual void rescan() = 0;
        virtual void exportPaymentProof(const TxID& id) = 0;
        virtual void checkNetworkAddress(const std::string& addr) = 0;

        virtual void importRecovery(const std::string& path) = 0;
        virtual void importDataFromJson(const std::string& data) = 0;
        virtual void exportDataToJson() = 0;
        virtual void exportTxHistoryToCsv() = 0;

        virtual void switchOnOffExchangeRates(bool isActive) = 0;
        virtual void switchOnOffNotifications(Notification::Type type, bool isActive) = 0;

        virtual void getNotifications() = 0;
        virtual void markNotificationAsRead(const ECC::uintBig& id) = 0;
        virtual void deleteNotification(const ECC::uintBig& id) = 0;

        virtual void getExchangeRates() = 0;
        virtual void getPublicAddress() = 0;
        virtual void getVerificationInfo() = 0;

        virtual void generateVouchers(uint64_t ownID, size_t count, AsyncCallback<const ShieldedVoucherList&>&& callback) = 0;
        virtual void getAssetInfo(Asset::ID) = 0;
        virtual void makeIWTCall(std::function<boost::any()>&& function, AsyncCallback<const boost::any&>&& resultCallback) = 0;

        // error (if any), shader output (if any), txid (if any)
        typedef AsyncCallback<const std::string&, const std::string&, const TxID&> ShaderCallback;
        virtual void callShader(const std::vector<uint8_t>& shader, const std::string& args, ShaderCallback&& cback) = 0;

        virtual void setMaxPrivacyLockTimeLimitHours(uint8_t limit) = 0;
        virtual void getMaxPrivacyLockTimeLimitHours(AsyncCallback<uint8_t>&& callback) = 0;

        virtual void setCoinConfirmationsOffset(uint32_t val) = 0;
        virtual void getCoinConfirmationsOffset(AsyncCallback<uint32_t>&& callback) = 0;

        virtual void enableBodyRequests(bool value) = 0;
        virtual ~IWalletModelAsync() = default;
    };
}
