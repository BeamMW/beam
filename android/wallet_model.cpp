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

#include "wallet_model.h"
#include "utility/logger.h"

#include <jni.h>
#include "common.h"

using namespace beam;
using namespace beam::io;
using namespace std;

WalletModel::WalletModel(IWalletDB::Ptr walletDB, const std::string& nodeAddr)
    : WalletClient(walletDB, nodeAddr)
{    
}

WalletModel::~WalletModel()
{
}

void WalletModel::onStatus(const WalletStatus& status)
{
    JNIEnv* env = Android_JNI_getEnv();

    jobject walletStatus = env->AllocObject(WalletStatusClass);

    setLongField(env, WalletStatusClass, walletStatus, "available", status.available);
    setLongField(env, WalletStatusClass, walletStatus, "unconfirmed", status.maturing);

    {
        jobject systemState = env->AllocObject(SystemStateClass);

        setLongField(env, SystemStateClass, systemState, "height", status.stateID.m_Height);
        setByteArrayField(env, SystemStateClass, systemState, "hash", status.stateID.m_Hash);

        jfieldID systemStateID = env->GetFieldID(WalletStatusClass, "system", "L" BEAM_JAVA_PATH "/entities/dto/SystemStateDTO;");
        env->SetObjectField(walletStatus, systemStateID, systemState);
    }

    ////////////////

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onStatus", "(L" BEAM_JAVA_PATH "/entities/dto/WalletStatusDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, walletStatus);
}

void WalletModel::onTxStatus(beam::ChangeAction action, const std::vector<beam::TxDescription>& items)
{
    LOG_DEBUG() << "onTxStatus()";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onTxStatus", "(I[L" BEAM_JAVA_PATH "/entities/dto/TxDescriptionDTO;)V");

    jobjectArray txItems = 0;

    if (!items.empty())
    {
        txItems = env->NewObjectArray(static_cast<jsize>(items.size()), TxDescriptionClass, NULL);

        for (int i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];

            jobject tx = env->AllocObject(TxDescriptionClass);

            setByteArrayField(env, TxDescriptionClass, tx, "id", item.m_txId);
            setLongField(env, TxDescriptionClass, tx, "amount", item.m_amount);
            setLongField(env, TxDescriptionClass, tx, "fee", item.m_fee);
            setLongField(env, TxDescriptionClass, tx, "change", item.m_change);
            setLongField(env, TxDescriptionClass, tx, "minHeight", item.m_minHeight);

            setStringField(env, TxDescriptionClass, tx, "peerId", to_string(item.m_peerId));
            setStringField(env, TxDescriptionClass, tx, "myId", to_string(item.m_myId));

            setByteArrayField(env, TxDescriptionClass, tx, "message", item.m_message);
            setLongField(env, TxDescriptionClass, tx, "createTime", item.m_createTime);
            setLongField(env, TxDescriptionClass, tx, "modifyTime", item.m_modifyTime);
            setBooleanField(env, TxDescriptionClass, tx, "sender", item.m_sender);
            setIntField(env, TxDescriptionClass, tx, "status", static_cast<jint>(item.m_status));

            env->SetObjectArrayElement(txItems, i, tx);
        }
    }

    env->CallStaticVoidMethod(WalletListenerClass, callback, action, txItems);
}

void WalletModel::onSyncProgressUpdated(int done, int total)
{
    LOG_DEBUG() << "onSyncProgressUpdated(" << done << ", " << total << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onSyncProgressUpdated", "(II)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, done, total);
}

void WalletModel::onChangeCalculated(beam::Amount change)
{
    LOG_DEBUG() << "onChangeCalculated(" << change << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onChangeCalculated", "(J)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, change);
}

void WalletModel::onAllUtxoChanged(const std::vector<beam::Coin>& utxosVec)
{
    LOG_DEBUG() << "onAllUtxoChanged()";

    JNIEnv* env = Android_JNI_getEnv();

    jobjectArray utxos = 0;

    if (!utxosVec.empty())
    {
        utxos = env->NewObjectArray(static_cast<jsize>(utxosVec.size()), UtxoClass, NULL);

        for (int i = 0; i < utxosVec.size(); ++i)
        {
            const auto& coin = utxosVec[i];

            jobject utxo = env->AllocObject(UtxoClass);

            setLongField(env, UtxoClass, utxo, "id", coin.m_ID.m_Idx);
            setLongField(env, UtxoClass, utxo, "amount", coin.m_ID.m_Value);
            setIntField(env, UtxoClass, utxo, "status", coin.m_status);
            setLongField(env, UtxoClass, utxo, "createHeight", coin.m_createHeight);
            setLongField(env, UtxoClass, utxo, "maturity", coin.m_maturity);
            setIntField(env, UtxoClass, utxo, "keyType", static_cast<jint>(coin.m_ID.m_Type));
            setLongField(env, UtxoClass, utxo, "confirmHeight", coin.m_confirmHeight);
            setLongField(env, UtxoClass, utxo, "lockHeight", coin.m_lockedHeight);

            if (coin.m_createTxId)
                setByteArrayField(env, UtxoClass, utxo, "createTxId", *coin.m_createTxId);

            if (coin.m_spentTxId)
                setByteArrayField(env, UtxoClass, utxo, "spentTxId", *coin.m_spentTxId);

            env->SetObjectArrayElement(utxos, i, utxo);
        }
    }

    //////////////////////////////////

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAllUtxoChanged", "([L" BEAM_JAVA_PATH "/entities/dto/UtxoDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, utxos);
}

void WalletModel::onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses)
{
    LOG_DEBUG() << "onAdrresses(" << own << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jobjectArray addrArray = 0;

    if (!addresses.empty())
    {
        addrArray = env->NewObjectArray(static_cast<jsize>(addresses.size()), WalletAddressClass, NULL);

        for (int i = 0; i < addresses.size(); ++i)
        {
            const auto& addrRef = addresses[i];

            jobject addr = env->AllocObject(WalletAddressClass);

            {
                setStringField(env, WalletAddressClass, addr, "walletID", to_string(addrRef.m_walletID));
                setStringField(env, WalletAddressClass, addr, "label", addrRef.m_label);
                setStringField(env, WalletAddressClass, addr, "category", addrRef.m_category);
                setLongField(env, WalletAddressClass, addr, "createTime", addrRef.m_createTime);
                setLongField(env, WalletAddressClass, addr, "duration", addrRef.m_duration);
                setLongField(env, WalletAddressClass, addr, "own", addrRef.m_OwnID);
            }

            env->SetObjectArrayElement(addrArray, i, addr);
        }
    }

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAdrresses", "(Z[L" BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, own, addrArray);
}

void WalletModel::onGeneratedNewAddress(const beam::WalletAddress& address)
{
    LOG_DEBUG() << "onGeneratedNewAddress()";

    JNIEnv* env = Android_JNI_getEnv();

    jobject addr = env->AllocObject(WalletAddressClass);

    {
        setStringField(env, WalletAddressClass, addr, "walletID", to_string(address.m_walletID));
        setStringField(env, WalletAddressClass, addr, "label", address.m_label);
        setStringField(env, WalletAddressClass, addr, "category", address.m_category);
        setLongField(env, WalletAddressClass, addr, "createTime", address.m_createTime);
        setLongField(env, WalletAddressClass, addr, "duration", address.m_duration);
        setLongField(env, WalletAddressClass, addr, "own", address.m_OwnID);
    }

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onGeneratedNewAddress", "(L" BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, addr);
}

void WalletModel::onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID)
{
}

void WalletModel::onNodeConnectionChanged(bool isNodeConnected)
{
    LOG_DEBUG() << "onNodeConnectedStatusChanged(" << isNodeConnected << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeConnectedStatusChanged", "(Z)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, isNodeConnected);
}

void WalletModel::onWalletError(beam::wallet::ErrorType error)
{
    LOG_DEBUG() << "onNodeConnectionFailed()";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeConnectionFailed", "()V");

    env->CallStaticVoidMethod(WalletListenerClass, callback);
}
