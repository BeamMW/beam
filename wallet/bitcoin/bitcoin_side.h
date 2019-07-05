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

#include "../swaps/second_side.h"
#include "../swaps/common.h"
#include "../base_transaction.h"
#include "bitcoin/bitcoin.hpp"
#include "bitcoin_bridge.h"

#include <memory>

namespace beam::wallet
{
    class BitcoinSide : public SecondSide, public std::enable_shared_from_this<BitcoinSide>
    {
    public:
        BitcoinSide(BaseTransaction& tx, std::shared_ptr<IBitcoinBridge> bitcoinBridge, bool isBeamSide);

        bool Initialize() override;
        bool InitLockTime() override;
        bool ValidateLockTime() override;
        void AddTxDetails(SetTxParameter& txParameters) override;
        bool ConfirmLockTx() override;
        bool SendLockTx() override;
        bool SendRefund() override;
        bool SendRedeem() override;
        bool IsLockTimeExpired() override;
        bool HasEnoughTimeToProcessLockTx() override;
        uint32_t GetTxTimeInBeamBlocks() const override;

        static bool CheckAmount(Amount amount, Amount feeRate);

    private:
        bool LoadSwapAddress();
        void InitSecret();
        libbitcoin::chain::script CreateAtomicSwapContract();
        bool RegisterTx(const std::string& rawTransaction, SubTxID subTxID);
        SwapTxState BuildLockTx();
        SwapTxState BuildWithdrawTx(SubTxID subTxID);
        void GetSwapLockTxConfirmations();
        bool SendWithdrawTx(SubTxID subTxID);
        uint64_t GetBlockCount();
        std::string GetWithdrawAddress() const;
        void SetTxError(const IBitcoinBridge::Error& error, SubTxID subTxID);

        void OnGetRawChangeAddress(const IBitcoinBridge::Error& error, const std::string& address);
        void OnFundRawTransaction(const IBitcoinBridge::Error& error, const std::string& hexTx, int changePos);
        void OnSignLockTransaction(const IBitcoinBridge::Error& error, const std::string& hexTx, bool complete);
        void OnCreateWithdrawTransaction(SubTxID subTxID, const IBitcoinBridge::Error& error, const std::string& hexTx);
        void OnDumpPrivateKey(SubTxID subTxID, const IBitcoinBridge::Error& error, const std::string& privateKey);
        void OnGetSwapLockTxConfirmations(const IBitcoinBridge::Error& error, const std::string& hexScript, double amount, uint16_t confirmations);
        void OnGetBlockCount(const IBitcoinBridge::Error& error, uint64_t blockCount);

    private:
        BaseTransaction& m_tx;
        std::shared_ptr<IBitcoinBridge> m_bitcoinBridge;
        bool m_isBtcOwner;
        uint64_t m_blockCount = 0;

        // TODO: make a separate struct
        // btc additional params
        uint16_t m_SwapLockTxConfirmations = 0;
        boost::optional<std::string> m_SwapLockRawTx;
        boost::optional<std::string> m_SwapWithdrawRawTx;
    };

}