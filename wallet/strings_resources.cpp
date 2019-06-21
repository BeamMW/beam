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

#include "wallet/strings_resources.h"

namespace beam
{
    const char kTimeStampFormat3x3[] = "%Y.%m.%d %H:%M:%S";
    const char kBEAM[] = "BEAM";
    const char kGROTH[] = "GROTH";
    // Coin statuses
    const char kCoinStatusAvailable[] = "Available";
    const char kCoinStatusUnavailable[] = "Unavailable";
    const char kCoinStatusSpent[] = "Spent";
    const char kCoinStatusMaturing[] = "Maturing";
    const char kCoinStatusOutgoing[] = "In progress(outgoing)";
    const char kCoinStatusIncoming[] = "In progress(incoming/change)";

    // Tx statuses
    const char kTxStatusPending[] = "pending";
    const char kTxStatusWaitingForSender[] = "waiting for sender";
    const char kTxStatusWaitingForReceiver[] = "waiting for receiver";
    const char kTxStatusSending[] = "sending";
    const char kTxStatusReceiving[] = "receiving";
    const char kTxStatusCancelled[] = "cancelled";
    const char kTxStatusSent[] = "sent";
    const char kTxStatusReceived[] = "received";
    const char kTxStatusFailed[] = "failed";
    const char kTxStatusCompleted[] = "completed";
    const char kTxStatusExpired[] = "expired";

    // Errors
    const char kErrorUnknownCoinStatus[] = "Unknown coin status";
    const char kErrorUnknowmTxStatus[] = "Unknown status";
    const char kErrorUnknownSwapCoin[] = "Unknow SwapCoin";
    const char kErrorInvalidWID[] = "invalid WID";
    const char kErrorTreasuryBadN[] = "bad n (roundoff)";
    const char kErrorTreasuryBadM[] = "bad m/n";
    const char kErrorTreasuryNothingRemains[] = "Nothing remains";
    const char kErrorTreasuryPlanNotFound[] = "plan not found";
    const char kErrorTreasuryInvalidResponse[] = "invalid response";
    const char kErrorAddrExprTimeInvalid[] = "Invalid address expiration time \"%1%\".";
    const char kErrorSeedPhraseInvalid[] = "Invalid seed phrase provided: %1%";
    const char kErrorSeedPhraseNotProvided[] = "Seed phrase has not been provided.";

    // Swap Tx statuses
    const char kSwapTxStatusInitial[] = "initial";
    const char kSwapTxStatusInvitation[] = "invitation";
    const char kSwapTxStatusBuildingBeamLockTX[] = "building Beam LockTX";
    const char kSwapTxStatusBuildingBeamRefundTX[] = "building Beam RefundTX";
    const char kSwapTxStatusBuildingBeamRedeemTX[] = "building Beam RedeemTX";
    const char kSwapTxStatusHandlingContractTX[] = "handling LockTX";
    const char kSwapTxStatusSendingRefundTX[] = "sending RefundTX";
    const char kSwapTxStatusSendingRedeemTX[] = "sending RedeemTX";
    const char kSwapTxStatusSendingBeamLockTX[] = "sending Beam LockTX";
    const char kSwapTxStatusSendingBeamRefundTX[] = "sending Beam RefundTX";
    const char kSwapTxStatusSendingBeamRedeemTX[] = "sending Beam RedeemTX";
    const char kSwapTxStatusCompleted[] = "completed";
    const char kSwapTxStatusCancelled[] = "cancelled";
    const char kSwapTxStatusAborted[] = "aborted";
    const char kSwapTxStatusFailed[] = "failed";
    const char kSwapTxStatusExpired[] = "expired";

    // Coins available for swap
    const char kSwapCoinBTC[] = "BTC";
    const char kSwapCoinLTC[] = "LTC";
    const char kSwapCoinQTUM[] = "QTUM";

    // Treasury messages
    const char kTreasuryConsumeRemaining[] = "Maturity=%1%, Consumed = %2% / %3%";
    const char kTreasuryDataHash[] = "Treasury data hash: %1%";
    const char kTreasuryRecoveredCoinsTitle[] = "Recovered coins: %1%";
    const char kTreasuryRecoveredCoin[] = "\t%1%, Height=%2%";
    const char kTreasuryBurstsTitle[] = "Total bursts: %1%";
    const char kTreasuryBurst[] = "\tHeight=%1%, Value=%2%";

    // Address
    const char kExprTime24h[] = "24h";
    const char kExprTimeNever[] = "never";
    const char kAllAddrExprChanged[] = "Expiration for all addresses  was changed to \"%1%\".";
    const char kAddrExprChanged[] = "Expiration for address %1% was changed to \"%2%\".";
    const char kAddrNewGenerated[] = "New address generated:\n\n%1%\n";
    const char kAddrNewGeneratedComment[] = "comment = %1%";
    const char kAddrListTableHead[] = "Addresses\n\n  %1%|%2%|%3%|%4%|%5%";
    const char kAddrListColumnComment[] = "comment";
    const char kAddrListColumnAddress[] = "address";
    const char kAddrListColumnActive[] = "active";
    const char kAddrListColumnExprDate[] = "expiration date";
    const char kAddrListColumnCreated[] = "created";
    const char kAddrListTableBody[] = "  %1% %2% %3% %4% %5%";

    // Seed phrase
    const char kSeedPhraseGeneratedTitle[] = "======\nGenerated seed phrase: \n\n\t";
    const char kSeedPhraseGeneratedMessage[] = "\n\n\tIMPORTANT\n\n\tYour seed phrase is the access key to all the cryptocurrencies in your wallet.\n\tPrint or write down the phrase to keep it in a safe or in a locked vault.\n\tWithout the phrase you will not be able to recover your money.\n======";
    const char kSeedPhraseReadTitle[] = "Generating seed phrase...";

    // Wallet info
    const char kWalletSummaryFormat[] = "____Wallet summary____\n\n%1%%2%\n%3%%4%\n\n%5%%6%\n%7%%8%\n%9%%10%\n%11%%12%\n%13%%14%\n%15%%16%\n%17%%18%\n%19%%20%\n%21%%22%\n\n";
    const char kWalletSummaryFieldCurHeight[] = "Current height";
    const char kWalletSummaryFieldCurStateID[] = "Current state ID";
    const char kWalletSummaryFieldAvailable[] = "Available";
    const char kWalletSummaryFieldMaturing[] = "Maturing";
    const char kWalletSummaryFieldInProgress[] = "In progress";
    const char kWalletSummaryFieldUnavailable[] = "Unavailable";
    const char kWalletSummaryFieldAvailableCoinbase[] = "Available coinbase";
    const char kWalletSummaryFieldTotalCoinbase[] = "Total coinbase";
    const char kWalletSummaryFieldAvaliableFee[] = "Avaliable fee";
    const char kWalletSummaryFieldTotalFee[] = "Total fee";
    const char kWalletSummaryFieldTotalUnspent[] = "Total unspent";
    const char kCoinsTableHeadFormat[] = "  | %1% | %2% | %3% | %4% | %5% | %6% |";
    const char kCoinColumnId[] = "ID";
    const char kCoinColumnMaturity[] = "Maturity";
    const char kCoinColumnStatus[] = "Status";
    const char kCoinColumnType[] = "Type";
    const char kCoinsTableFormat[] = "    %1%   %2%   %3%   %4%   %5%   %6%  ";

    // Tx history
    const char kTxHistoryTableHead[] = "TRANSACTIONS\n\n  | %1% | %2% | %3% | %4% | %5% | %6% |";
    const char kTxHistoryTableFormat[] = "    %1%   %2%   %3%   %4%   %5%   %6%  ";
    const char kTxHistoryColumnDatetTime[] = "datetime";
    const char kTxHistoryColumnDirection[] = "direction";
    const char kTxHistoryColumnAmount[] = "amount, BEAM";
    const char kTxHistoryColumnStatus[] = "status";
    const char kTxHistoryColumnId[] = "ID";
    const char kTxHistoryColumnKernelId[] = "kernel ID";
    const char kTxDirectionSelf[] = "self transaction";
    const char kTxDirectionOut[] = "outgoing";
    const char kTxDirectionIn[] = "incoming";
    const char kTxHistoryEmpty[] = "No transactions";
    const char kSwapTxHistoryEmpty[] = "No swap transactions";
    const char kSwapTxHistoryTableHead[] = "SWAP TRANSACTIONS\n\n  | %1% | %2% | %3% | %4% | %5% | %6% |";
    const char kSwapTxHistoryTableFormat[] = "    %1%   %2%   %3%   %4%   %5%   %6%  ";
    const char kTxHistoryColumnSwapAmount[] = "swap amount";
    const char kTxHistoryColumnSwapType[] = "swap type";

}
