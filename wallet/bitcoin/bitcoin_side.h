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

#include "wallet/swaps/second_side.h"
#include "wallet/swaps/common.h"
#include "wallet/base_transaction.h"
#include "bridge.h"
#include "settings_provider.h"

#include <memory>

namespace beam::wallet
{
    class BitcoinSide : public SecondSide, public std::enable_shared_from_this<BitcoinSide>
    {
    public:
        BitcoinSide(BaseTransaction& tx, bitcoin::IBridge::Ptr bitcoinBridge, bitcoin::ISettingsProvider& settingsProvider, bool isBeamSide);
        virtual ~BitcoinSide();

        bool Initialize() override;
        bool InitLockTime() override;
        bool ValidateLockTime() override;
        void AddTxDetails(SetTxParameter& txParameters) override;
        bool ConfirmLockTx() override;
        bool ConfirmRefundTx() override;
        bool ConfirmRedeemTx() override;
        bool SendLockTx() override;
        bool SendRefund() override;
        bool SendRedeem() override;
        bool IsLockTimeExpired() override;
        bool HasEnoughTimeToProcessLockTx() override;
        bool IsQuickRefundAvailable() override;

        static bool CheckAmount(Amount amount, Amount feeRate);

    protected:
        virtual uint32_t GetLockTxEstimatedTimeInBeamBlocks() const;
        virtual uint8_t GetAddressVersion() const;

        Amount GetFeeRate() const;
        Amount GetFeeRate(SubTxID subTxID) const;
        uint16_t GetTxMinConfirmations() const;
        uint32_t GetLockTimeInBlocks() const;
        double GetBlocksPerHour() const;

    private:
        bool LoadSwapAddress();
        void InitSecret();
        void InitLocalKeys();
        bool RegisterTx(const std::string& rawTransaction, SubTxID subTxID);
        SwapTxState BuildLockTx();
        SwapTxState BuildWithdrawTx(SubTxID subTxID);
        void GetSwapLockTxConfirmations();
        void GetRefundTxConfirmations();
        void GetRedeemTxConfirmations();
        bool SendWithdrawTx(SubTxID subTxID);
        uint64_t GetBlockCount(bool notify = false);
        std::string GetWithdrawAddress() const;
        void SetTxError(const bitcoin::IBridge::Error& error, SubTxID subTxID);
        void FillWithdrawTx(SubTxID subTxID);

        void OnGetRawChangeAddress(const bitcoin::IBridge::Error& error, const std::string& address);
        void OnFundRawTransaction(const bitcoin::IBridge::Error& error, const std::string& hexTx, int changePos);
        void OnSignLockTransaction(const bitcoin::IBridge::Error& error, const std::string& hexTx, bool complete);
        void OnCreateWithdrawTransaction(SubTxID subTxID, const bitcoin::IBridge::Error& error, const std::string& hexTx);
        void OnGetSwapLockTxConfirmations(const bitcoin::IBridge::Error& error, const std::string& hexScript, Amount amount, uint32_t confirmations);
        void OnGetBlockCount(const bitcoin::IBridge::Error& error, uint64_t blockCount, bool notify);

    private:
        BaseTransaction& m_tx;
        bitcoin::IBridge::Ptr m_bitcoinBridge;
        bitcoin::ISettingsProvider& m_settingsProvider;
        bool m_isBtcOwner;
        uint64_t m_blockCount = 0;

        uint32_t m_SwapLockTxConfirmations = 0;
        uint32_t m_RefundTxConfirmations = 0;
        uint32_t m_RedeemTxConfirmations = 0;
        uint32_t m_LockTxValuePosition = 0;
        boost::optional<std::string> m_SwapLockRawTx;
        boost::optional<std::string> m_SwapWithdrawRawTx;
    };
}