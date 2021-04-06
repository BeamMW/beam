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

package com.mw.beam.beamwallet.core.listeners;

import com.mw.beam.beamwallet.core.entities.dto.WalletStatusDTO;
import com.mw.beam.beamwallet.core.entities.dto.UtxoDTO;
import com.mw.beam.beamwallet.core.entities.dto.TxDescriptionDTO;
import com.mw.beam.beamwallet.core.entities.dto.WalletAddressDTO;
import com.mw.beam.beamwallet.core.entities.dto.PaymentInfoDTO;
import com.mw.beam.beamwallet.core.entities.dto.NotificationDTO;
import com.mw.beam.beamwallet.core.entities.dto.ExchangeRateDTO;
import com.mw.beam.beamwallet.core.entities.dto.VersionInfoDTO;

import com.mw.beam.beamwallet.core.entities.Wallet;

public class WalletListener
{
	public static Wallet wallet;

	static void onStatus(WalletStatusDTO status)
	{
		System.out.println(">>>>>>>>>>>>>> async status in Java, available=" + status.available/1000000 + " BEAM and " + status.available%1000000 + " GROTH, maturing=" + status.maturing);
		System.out.println("height is " + status.system.height);
	}

	static void onTxStatus(int action, TxDescriptionDTO[] tx)
	{
		System.out.println(">>>>>>>>>>>>>> async onTxStatus in Java");

		switch(action)
		{
		case 0://Added: 
			System.out.println("onTxStatus [ADDED]");
			break;
		case 1://Removed: 
			System.out.println("onTxStatus [REMOVED]");
			break;
		case 2://Updated: 
			System.out.println("onTxStatus [UPDATED]");
			break;
		case 3://Reset:
			System.out.println("onTxStatus [RESET]");
			break;
		}

		if(tx != null)
		{
			System.out.println("+-------------------------------------------------------");
			System.out.println("| TRANSACTIONS");
			System.out.println("+----------------------------------------------------------------------");
			System.out.println("| date:                         | amount:       | status:");
			System.out.println("+-------------------------------+---------------+-----------------------");

			for(int i = 0; i < tx.length; i++)
			{
				System.out.println("| " + new java.util.Date(tx[i].createTime*1000)
					+ "\t| " + tx[i].amount
					+ "\t| " + tx[i].status);
			}

			System.out.println("+-------------------------------------------------------");			
		}
	}

	static void onSyncProgressUpdated(int done, int total)
	{
		System.out.println(">>>>>>>>>>>>>> async onSyncProgressUpdated in Java [ " + done + " / " + total + " ]");
	}

    static void onNodeSyncProgressUpdated(int done, int total)
	{
		System.out.println(">>>>>>>>>>>>>> async onNodeSyncProgressUpdated in Java [ " + done + " / " + total + " ]");
	}

	static void onChangeCalculated(long amount)
	{
		System.out.println(">>>>>>>>>>> onChangeCalculated(" + amount + ") called");
	}

	static void onShieldedCoinsSelectionCalculated(long minFee)
	{
		System.out.println(">>>>>>>>>>> onShieldedCoinsSelectionCalculated(" + minFee + ") called");
	}

	static void onNormalUtxoChanged(UtxoDTO[] utxos)
	{
		System.out.println(">>>>>>>>>>>>>> async onNormalUtxoChanged in Java");
		
		if(utxos != null)
		{
			System.out.println("utxos length: " + utxos.length);

			System.out.println("+-------------------------------------------------------");
			System.out.println("| UTXO");
			System.out.println("+-------------------------------------------------------");
			System.out.println("| id:   | amount:       | type:");
			System.out.println("+-------+---------------+-------------------------------");

			for(int i = 0; i < utxos.length; i++)
			{
				System.out.println("| " + utxos[i].id 
					+ "\t| "  + utxos[i].amount
					+ "\t| "  + utxos[i].keyType);
			}

			System.out.println("+-------------------------------------------------------");			
		}
	}

	static void onAddresses(boolean own, WalletAddressDTO[] addresses)
	{
		System.out.println(">>>>>>>>>>> onAddresses(" + own + ") called");

		if(addresses != null)
		{
			System.out.println("+-------------------------------------------------------");	

			for(int i = 0; i < addresses.length; i++)
			{
				System.out.println(addresses[i].walletID);
			}

			System.out.println("+-------------------------------------------------------");	
		}
	}

	static void onGeneratedNewAddress(WalletAddressDTO addr)
	{
		System.out.println(">>>>>>>>>>> onGeneratedNewAddress() called");

		System.out.println(addr.walletID);
	}

	static void onNewAddressFailed()
	{
		System.out.println(">>>>>>>>>>> onNewAddressFailed() called");
	}

	static void onNodeConnectedStatusChanged(boolean isNodeConnected)
	{
		System.out.println(">>>>>>>>>>>>>> async onNodeConnectedStatusChanged(" + isNodeConnected + ") in Java");
	}

	static void onNodeConnectionFailed(int error)
	{
		System.out.println(">>>>>>>>>>>>>> async onNodeConnectionFailed() in Java");
	}

    static void onCantSendToExpired()
	{
		System.out.println(">>>>>>>>>>>>>> async onCantSendToExpired() in Java");
	}

    static void onPaymentProofExported(String txId, PaymentInfoDTO proof)
    {
        System.out.println(">>>>>>>>>>>>>> async onPaymentProofExported() in Java");
    }

    static void onStartedNode()
    {
        System.out.println(">>>>>>>>>>>>>> async onStartedNode() in Java");
    }

    static void onStoppedNode()
    {
        System.out.println(">>>>>>>>>>>>>> async onStoppedNode() in Java");
    }

    static void onNodeCreated()
    {
        System.out.println(">>>>>>>>>>>>>> async onNodeCreated() in Java");
    }

    static void onNodeDestroyed()
    {
        System.out.println(">>>>>>>>>>>>>> async onNodeDestroyed() in Java");
    }

    static void onFailedToStartNode()
    {
        System.out.println(">>>>>>>>>>>>>> async onFailedToStartNode() in Java");
    }

    static void onCoinsByTx(UtxoDTO[] utxos)
    {
        System.out.println(">>>>>>>>>>> onCoinsByTx called");
    }

    static void onNodeThreadFinished()
    {
        System.out.println(">>>>>>>>>>> onNodeThreadFinished called");
    }

    static void onImportRecoveryProgress(long done, long total)
    {
        System.out.println(">>>>>>>>>>>>>> async onImportRecoveryProgress in Java [ " + done + " / " + total + " ]");
    }

    static void onImportDataFromJson(boolean isOk)
    {
        System.out.println(">>>>>>>>>>>>>> async onImportDataFromJson(" + isOk +") in Java");
    }

    static void onExportDataToJson(String data)
    {
        System.out.println(">>>>>>>>>>>>>> async onExportDataToJson(" + data +") in Java");
    }

	static void onNewVersionNotification(int action, NotificationDTO notificationInfo, VersionInfoDTO content)
    {
		System.out.println(">>>>>>>>>>>>>> async onNewVersionNotification in Java");
		switch(action)
		{
			case 0://Added: 
				System.out.println("onNewVersionNotification [ADDED]");
				break;
			case 1://Removed: 
				System.out.println("onNewVersionNotification [REMOVED]");
				break;
			case 2://Updated: 
				System.out.println("onNewVersionNotification [UPDATED]");
				break;
			case 3://Reset:
				System.out.println("onNewVersionNotification [RESET]");
				break;
		}
		System.out.println("Id: " + notificationInfo.id);

		String stateString;
		switch(notificationInfo.state)
		{
			case 0:
				stateString = "Unread";
				break;
			case 1: 
				stateString = "Read";
				break;
			case 2: 
				stateString = "Deleted";
				break;
			default:
				stateString = "Unknown";
		}
		System.out.println("State: " + stateString);
		System.out.println("CreateTime: " + notificationInfo.createTime);
		
		String applicationString;
		switch(content.application)
		{
			case 0:
				applicationString = "DesktopWallet";
				break;
			case 1:
				applicationString = "AndroidWallet";
				break;
			case 2:
				applicationString = "IOSWallet";
				break;
			case 3:
				applicationString = "Unknown";
				break;
			default:
				applicationString = "Unknown";
		}
		System.out.println("Application: " + applicationString);
		System.out.println("versionMajor: " + content.versionMajor);
		System.out.println("versionMinor: " + content.versionMinor);
		System.out.println("versionRevision: " + content.versionRevision);
    }

	static void onAddressChangedNotification(int action, NotificationDTO notificationInfo, WalletAddressDTO content)
    {
		System.out.println(">>>>>>>>>>>>>> async onAddressChangedNotification in Java");
	}

	static void onTransactionFailedNotification(int action, NotificationDTO notificationInfo, TxDescriptionDTO content)
    {
		System.out.println(">>>>>>>>>>>>>> async onTransactionFailedNotification in Java");
	}

	static void onTransactionCompletedNotification(int action, NotificationDTO notificationInfo, TxDescriptionDTO content)
    {
		System.out.println(">>>>>>>>>>>>>> async onTransactionCompletedNotification in Java");
	}

	static void onBeamNewsNotification(int action)
    {
		System.out.println(">>>>>>>>>>>>>> async onBeamNewsNotification in Java");
	}

	static void onExchangeRates(ExchangeRateDTO[] rates)
    {
        System.out.println(">>>>>>>>>>>>>> async onExchangeRates in Java");
		
		if(rates != null)
		{
			System.out.println("rates length: " + rates.length);

			System.out.println("+-------------------------------------------------------");
			System.out.println("| RATE");
			System.out.println("+-------------------------------------------------------");
			System.out.println("| Currency: | amount:       | Unit:	| Updated:");
			System.out.println("+-----------+---------------+-------+-------------------");

			for(int i = 0; i < rates.length; i++)
			{
				System.out.println( "| " + rates[i].currency 
								+ "\t| " + rates[i].amount
								+ "\t| " + rates[i].unit
								+ "\t| " + rates[i].updateTime);
			}

			System.out.println("+-------------------------------------------------------");			
		}
    }
}
