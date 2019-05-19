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

package com.mw.beam.beamwallet.core.entities;

import java.util.*; 
import com.mw.beam.beamwallet.core.entities.dto.WalletAddressDTO;
import com.mw.beam.beamwallet.core.entities.dto.PaymentInfoDTO;

public class Wallet
{
	long _this;

    // implemented
    public native void getWalletStatus();
    public native void getUtxosStatus();
    public native void syncWithNode();
    public native void sendMoney(String receiver, String comment, long amount, long fee);
    public native void calcChange(long amount);
    public native void getAddresses(boolean own);
    public native void generateNewAddress();
    public native void saveAddress(WalletAddressDTO address, boolean own);
    public native void saveAddressChanges(String addr, String name, boolean isNever, boolean makeActive, boolean makeExpired);
    public native void cancelTx(String id);
    public native void deleteTx(String id);
    public native void deleteAddress(String walletID);
    public native void changeWalletPassword(String password);
    public native boolean checkWalletPassword(String password);
    public native void getPaymentInfo(String txID);
    public native PaymentInfoDTO verifyPaymentInfo(String paymentInfo);
    public native void getCoinsByTx(String txID);
    public native void changeNodeAddress(String address);

    // not implemented
    public native void changeCurrentWalletIDs(); //const beam::WalletID& senderID, const beam::WalletID& receiverID);
    public native void setNodeAddress(); //const std::string& addr);

}

