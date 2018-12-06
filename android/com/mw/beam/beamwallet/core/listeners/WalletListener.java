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

import com.mw.beam.beamwallet.core.entities.WalletStatus;
import com.mw.beam.beamwallet.core.entities.Utxo;
import com.mw.beam.beamwallet.core.entities.TxDescription;
import com.mw.beam.beamwallet.core.entities.TxPeer;
import com.mw.beam.beamwallet.core.entities.WalletAddress;

import com.mw.beam.beamwallet.core.entities.Wallet;

public class WalletListener
{
	public static Wallet wallet;

	static void onStatus(WalletStatus status)
	{
		System.out.println(">>>>>>>>>>>>>> async status in Java, available=" + status.available/1000000 + " BEAM and " + status.available%1000000 + " GROTH, unconfirmed=" + status.unconfirmed);
		System.out.println("height is " + status.system.height);
	}

	static void onTxStatus(int action, TxDescription[] tx)
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

	static void onTxPeerUpdated(TxPeer[] peers)
	{
		System.out.println(">>>>>>>>>>>>>> async onTxPeerUpdated in Java");

		for(int i = 0; i < peers.length; i++)
		{
			System.out.println("peer.label: " + peers[i].label + " peer.address: " + peers[i].address);
		}
	}

	static void onSyncProgressUpdated(int done, int total)
	{
		System.out.println(">>>>>>>>>>>>>> async onSyncProgressUpdated in Java [ " + done + " / " + total + " ]");
	}

	static void onChangeCalculated(long amount)
	{
		System.out.println(">>>>>>>>>>> onChangeCalculated(" + amount + ") called");
	}

	static void onAllUtxoChanged(Utxo[] utxos)
	{
		System.out.println(">>>>>>>>>>>>>> async onAllUtxoChanged in Java");
		
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

	static void onAdrresses(boolean own, WalletAddress[] addresses)
	{
		System.out.println(">>>>>>>>>>> onAdrresses(" + own + ") called");

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

	static void onGeneratedNewAddress(WalletAddress addr)
	{
		System.out.println(">>>>>>>>>>> onGeneratedNewAddress() called");

		System.out.println(addr.walletID);
	}

	static void onNodeConnectedStatusChanged(boolean isNodeConnected)
	{
		System.out.println(">>>>>>>>>>>>>> async onNodeConnectedStatusChanged(" + isNodeConnected + ") in Java");
	}

	static void onNodeConnectionFailed()
	{
		System.out.println(">>>>>>>>>>>>>> async onNodeConnectionFailed() in Java");
	}
}
