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

#include "wallet/transactions/swaps/second_side.h"
#include "wallet/transactions/swaps/common.h"
#include "wallet/core/base_transaction.h"
#include "bridge.h"
#include "settings_provider.h"

#include <memory>

namespace beam::wallet
{
    class EthereumSide : public SecondSide, public std::enable_shared_from_this<EthereumSide>
    {
    public:
        EthereumSide(BaseTransaction& tx, ethereum::IBridge::Ptr ethBridge, ethereum::ISettingsProvider& settingsProvider, bool isBeamSide);
        virtual ~EthereumSide();

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

    private:
        uint64_t GetBlockCount(bool notify = false);
        void InitSecret();

        uint16_t GetTxMinConfirmations() const;
        uint32_t GetLockTimeInBlocks() const;
        double GetBlocksPerHour() const;
        uint32_t GetLockTxEstimatedTimeInBeamBlocks() const;

        bool ConfirmWithdrawTx(SubTxID subTxID);
        void GetWithdrawTxConfirmations(SubTxID subTxID);

        ECC::uintBig GetSecret() const;
        ByteBuffer GetSecretHash() const;

        ECC::uintBig GetGas(SubTxID subTxID) const;
        ECC::uintBig GetGasPrice(SubTxID subTxID) const;

        ethereum::IBridge::short_hash GetContractAddress() const;
        std::string GetContractAddressStr() const;

        bool SendWithdrawTx(SubTxID subTxID);
        beam::ByteBuffer BuildWithdrawTxData(SubTxID subTxID);
        beam::ByteBuffer BuildRedeemTxData();
        beam::ByteBuffer BuildRefundTxData();

        bool IsERC20Token() const;        
        beam::ByteBuffer BuildLockTxData();
        ECC::uintBig GetSwapAmount() const;
        bool IsHashLockScheme() const;
        void SetTxError(const ethereum::IBridge::Error& error, SubTxID subTxID);

    private:
        BaseTransaction& m_tx;
        ethereum::IBridge::Ptr m_ethBridge;
        ethereum::ISettingsProvider& m_settingsProvider;
        bool m_isEthOwner;
        bool m_isWithdrawTxSent = false;
        uint64_t m_blockCount = 0;

        uint32_t m_SwapLockTxConfirmations = 0;
        uint64_t m_SwapLockTxBlockNumber = 0;
        uint32_t m_WithdrawTxConfirmations = 0;
        uint64_t m_WithdrawTxBlockNumber = 0;
    };
} // namespace beam::wallet