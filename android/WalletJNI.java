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

import com.mw.beam.beamwallet.core.*;
import com.mw.beam.beamwallet.core.entities.*;
import com.mw.beam.beamwallet.core.entities.dto.*;
import com.mw.beam.beamwallet.core.listeners.*;

import java.util.Arrays;

public class WalletJNI
{
	public static void main(String[] args)
	{
		System.out.println("Start Wallet JNI test...");

		Api api = new Api();

        {
            System.out.println("Test address...");
            System.out.println("invalid address: " + api.checkReceiverAddress("12"));
            System.out.println("valid address: " + api.checkReceiverAddress("1232134"));
        }

		{
			System.out.println("Test mnemonic...");
			String[] phrases = api.createMnemonic();

			System.out.println(Arrays.toString(phrases));
		}

		Wallet wallet;

		String nodeAddr = "172.104.249.212:8101";

		if(api.isWalletInitialized("test"))
		{
			wallet = api.openWallet("1.0.0.0", nodeAddr, "test", "123");

			System.out.println(wallet == null ? "wallet opening error" : "wallet successfully opened");
		}
		else
		{
			wallet = api.createWallet("1.0.0.0", nodeAddr, "test", "123", "garbage;wild;fruit;vicious;jungle;snack;arrange;pink;scorpion;speed;used;frozen;", false);

			System.out.println(wallet == null ? "wallet creation error" : "wallet successfully created");
		}

		WalletListener.wallet = wallet;

		wallet.syncWithNode();

		boolean sendAttempt = false;
		boolean addrGenearteAttempt = false;

		while(true)
		{
			System.out.println("Show info about wallet.");

			// call async wallet requests
			{
				wallet.getWalletStatus();
				wallet.getUtxosStatus();
				wallet.getAddresses(true);
				wallet.getAddresses(false);

				// if(!addrGenearteAttempt)
				// {
				// 	addrGenearteAttempt = true;
				// 	wallet.generateNewAddress();
				// }

				if(!sendAttempt)
				{
					sendAttempt = true;

					wallet.sendMoney("", "fbac2507faf499581aff0a2b97bccf5e4705aa36714ca14a529e98e8c4641ab7", "test comment", 1500, 10);
				}
			}
			try
			{
				Thread.sleep(5000);
			}
			catch(InterruptedException e) {}
		}
	}
}
