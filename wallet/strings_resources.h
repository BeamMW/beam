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

namespace beam
{
    extern const char kTimeStampFormat3x3[];
    extern const char kBEAM[];
    extern const char kGROTH[];
    // Coin statuses
    extern const char kCoinStatusAvailable[];
    extern const char kCoinStatusUnavailable[];
    extern const char kCoinStatusSpent[];
    extern const char kCoinStatusMaturing[];
    extern const char kCoinStatusOutgoing[];
    extern const char kCoinStatusIncoming[];
    // Tx statuses
    extern const char kTxStatusPending[];
    extern const char kTxStatusWaitingForSender[];
    extern const char kTxStatusWaitingForReceiver[];
    extern const char kTxStatusSending[];
    extern const char kTxStatusReceiving[];
    extern const char kTxStatusCancelled[];
    extern const char kTxStatusSent[];
    extern const char kTxStatusReceived[];
    extern const char kTxStatusFailed[];
    extern const char kTxStatusCompleted[];
    extern const char kTxStatusExpired[];
    // Errors
    extern const char kErrorUnknownCoinStatus[];
    extern const char kErrorUnknowmTxStatus[];
    extern const char kErrorUnknownSwapCoin[];
    extern const char kErrorInvalidWID[];
    extern const char kErrorTreasuryBadN[];
    extern const char kErrorTreasuryBadM[];
    extern const char kErrorTreasuryNothingRemains[];
    extern const char kErrorTreasuryPlanNotFound[];
    extern const char kErrorTreasuryInvalidResponse[];
    extern const char kErrorAddrExprTimeInvalid[];
    extern const char kErrorSeedPhraseInvalid[];
    extern const char kErrorSeedPhraseNotProvided[];
    extern const char kErrorTxIdParamReqired[];
    extern const char kErrorTxWithIdNotFound[];
    extern const char kErrorPpExportFailed[];
    extern const char kErrorPpCannotExportForReceiver[];
    extern const char kErrorPpExportFailedTxNotCompleted[];
    extern const char kErrorPpNotProvided[];
    extern const char kErrorPpInvalid[];
    // Swap Tx statuses
    extern const char kSwapTxStatusInitial[];
    extern const char kSwapTxStatusInvitation[];
    extern const char kSwapTxStatusBuildingBeamLockTX[];
    extern const char kSwapTxStatusBuildingBeamRefundTX[];
    extern const char kSwapTxStatusBuildingBeamRedeemTX[];
    extern const char kSwapTxStatusHandlingContractTX[];
    extern const char kSwapTxStatusSendingRefundTX[];
    extern const char kSwapTxStatusSendingRedeemTX[];
    extern const char kSwapTxStatusSendingBeamLockTX[];
    extern const char kSwapTxStatusSendingBeamRefundTX[];
    extern const char kSwapTxStatusSendingBeamRedeemTX[];
    extern const char kSwapTxStatusCompleted[];
    extern const char kSwapTxStatusCancelled[];
    extern const char kSwapTxStatusAborted[];
    extern const char kSwapTxStatusFailed[];
    extern const char kSwapTxStatusExpired[];
    // Coins available for swap
    extern const char kSwapCoinBTC[];
    extern const char kSwapCoinLTC[];
    extern const char kSwapCoinQTUM[];
    // Treasury messages
    extern const char kTreasuryConsumeRemaining[];
    extern const char kTreasuryDataHash[];
    extern const char kTreasuryRecoveredCoinsTitle[];
    extern const char kTreasuryRecoveredCoin[];
    extern const char kTreasuryBurstsTitle[];
    extern const char kTreasuryBurst[];
    // Address
    extern const char kExprTime24h[];
    extern const char kExprTimeNever[];
    extern const char kAllAddrExprChanged[];
    extern const char kAddrExprChanged[];
    extern const char kAddrNewGenerated[];
    extern const char kAddrNewGeneratedComment[];
    extern const char kAddrListTableHead[];
    extern const char kAddrListColumnComment[];
    extern const char kAddrListColumnAddress[];
    extern const char kAddrListColumnActive[];
    extern const char kAddrListColumnExprDate[];
    extern const char kAddrListColumnCreated[];
    extern const char kAddrListTableBody[];
    // Seed phrase
    extern const char kSeedPhraseGeneratedTitle[];
    extern const char kSeedPhraseGeneratedMessage[];
    extern const char kSeedPhraseReadTitle[];
    // Wallet info
    extern const char kWalletSummaryFormat[];
    extern const char kWalletSummaryFieldCurHeight[];
    extern const char kWalletSummaryFieldCurStateID[];
    extern const char kWalletSummaryFieldAvailable[];
    extern const char kWalletSummaryFieldMaturing[];
    extern const char kWalletSummaryFieldInProgress[];
    extern const char kWalletSummaryFieldUnavailable[];
    extern const char kWalletSummaryFieldAvailableCoinbase[];
    extern const char kWalletSummaryFieldTotalCoinbase[];
    extern const char kWalletSummaryFieldAvaliableFee[];
    extern const char kWalletSummaryFieldTotalFee[];
    extern const char kWalletSummaryFieldTotalUnspent[];
    extern const char kCoinsTableHeadFormat[];
    extern const char kCoinColumnId[];
    extern const char kCoinColumnMaturity[];
    extern const char kCoinColumnStatus[];
    extern const char kCoinColumnType[];
    extern const char kCoinsTableFormat[];
    // Tx history
    extern const char kTxHistoryTableHead[];
    extern const char kTxHistoryTableFormat[];
    extern const char kTxHistoryColumnDatetTime[];
    extern const char kTxHistoryColumnDirection[];
    extern const char kTxHistoryColumnAmount[];
    extern const char kTxHistoryColumnStatus[];
    extern const char kTxHistoryColumnId[];
    extern const char kTxHistoryColumnKernelId[];
    extern const char kTxDirectionSelf[];
    extern const char kTxDirectionOut[];
    extern const char kTxDirectionIn[];
    extern const char kTxHistoryEmpty[];
    extern const char kSwapTxHistoryEmpty[];
    extern const char kSwapTxHistoryTableHead[];
    extern const char kSwapTxHistoryTableFormat[];
    extern const char kTxHistoryColumnSwapAmount[];
    extern const char kTxHistoryColumnSwapType[];
    // Tx Details
    extern const char kTxDetailsFormat[];
    extern const char kTxDetailsFailReason[];

    extern const char kPpExportedFrom[];
}
