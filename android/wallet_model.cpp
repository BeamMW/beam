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
using namespace beam::wallet;
using namespace beam::io;
using namespace std;

namespace
{
    jobjectArray convertCoinsToJObject(JNIEnv* env, const std::vector<Coin>& utxosVec)
    {
        jobjectArray utxos = 0;

        if (!utxosVec.empty())
        {
            utxos = env->NewObjectArray(static_cast<jsize>(utxosVec.size()), UtxoClass, NULL);

            for (int i = 0; i < utxosVec.size(); ++i)
            {
                const auto& coin = utxosVec[i];

                jobject utxo = env->AllocObject(UtxoClass);

                setLongField(env, UtxoClass, utxo, "id", coin.m_ID.m_Idx);
                setStringField(env, UtxoClass, utxo, "stringId", coin.toStringID());
                setLongField(env, UtxoClass, utxo, "amount", coin.m_ID.m_Value);
                setIntField(env, UtxoClass, utxo, "status", coin.m_status);
                setLongField(env, UtxoClass, utxo, "maturity", coin.m_maturity);
                setIntField(env, UtxoClass, utxo, "keyType", static_cast<jint>(coin.m_ID.m_Type));
                setLongField(env, UtxoClass, utxo, "confirmHeight", coin.m_confirmHeight);

                if (coin.m_createTxId)
                    setStringField(env, UtxoClass, utxo, "createTxId", to_hex(coin.m_createTxId->data(), coin.m_createTxId->size()));

                if (coin.m_spentTxId)
                    setStringField(env, UtxoClass, utxo, "spentTxId", to_hex(coin.m_spentTxId->data(), coin.m_spentTxId->size()));

                env->SetObjectArrayElement(utxos, i, utxo);

                env->DeleteLocalRef(utxo);
            }
        }

        return utxos;
    }
}

WalletModel::WalletModel(IWalletDB::Ptr walletDB, IPrivateKeyKeeper::Ptr keyKeeper, const std::string& nodeAddr, Reactor::Ptr reactor)
    : WalletClient(walletDB, nodeAddr, reactor, keyKeeper)
{    
}

WalletModel::~WalletModel()
{
    stopReactor();
}

void WalletModel::onStatus(const WalletStatus& status)
{
    JNIEnv* env = Android_JNI_getEnv();

    jobject walletStatus = env->AllocObject(WalletStatusClass);

    setLongField(env, WalletStatusClass, walletStatus, "available", status.available);
    setLongField(env, WalletStatusClass, walletStatus, "receiving", status.receiving);
    setLongField(env, WalletStatusClass, walletStatus, "sending", status.sending);
    setLongField(env, WalletStatusClass, walletStatus, "maturing", status.maturing);

    {
        jobject systemState = env->AllocObject(SystemStateClass);

        setLongField(env, SystemStateClass, systemState, "height", status.stateID.m_Height);
        setStringField(env, SystemStateClass, systemState, "hash", to_hex(status.stateID.m_Hash.m_pData, status.stateID.m_Hash.nBytes));

        jfieldID systemStateID = env->GetFieldID(WalletStatusClass, "system", "L" BEAM_JAVA_PATH "/entities/dto/SystemStateDTO;");
        env->SetObjectField(walletStatus, systemStateID, systemState);

        env->DeleteLocalRef(systemState);
    }

    ////////////////

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onStatus", "(L" BEAM_JAVA_PATH "/entities/dto/WalletStatusDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, walletStatus);

    env->DeleteLocalRef(walletStatus);
}

void WalletModel::onTxStatus(ChangeAction action, const std::vector<TxDescription>& items)
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

            setStringField(env, TxDescriptionClass, tx, "id", to_hex(item.m_txId.data(), item.m_txId.size()));
            setLongField(env, TxDescriptionClass, tx, "amount", item.m_amount);
            setLongField(env, TxDescriptionClass, tx, "fee", item.m_fee);
            setLongField(env, TxDescriptionClass, tx, "change", item.m_change);
            setLongField(env, TxDescriptionClass, tx, "minHeight", item.m_minHeight);

            setStringField(env, TxDescriptionClass, tx, "peerId", to_string(item.m_peerId));
            setStringField(env, TxDescriptionClass, tx, "myId", to_string(item.m_myId));

            setStringField(env, TxDescriptionClass, tx, "message", string(item.m_message.begin(), item.m_message.end()));
            setLongField(env, TxDescriptionClass, tx, "createTime", item.m_createTime);
            setLongField(env, TxDescriptionClass, tx, "modifyTime", item.m_modifyTime);
            setBooleanField(env, TxDescriptionClass, tx, "sender", item.m_sender);
            setBooleanField(env, TxDescriptionClass, tx, "selfTx", item.m_selfTx);
            setIntField(env, TxDescriptionClass, tx, "status", static_cast<jint>(item.m_status));
            setStringField(env, TxDescriptionClass, tx, "kernelId", to_hex(item.m_kernelID.m_pData, item.m_kernelID.nBytes));
            setIntField(env, TxDescriptionClass, tx, "failureReason", static_cast<jint>(item.m_failureReason));

            env->SetObjectArrayElement(txItems, i, tx);

            env->DeleteLocalRef(tx);
        }
    }

    env->CallStaticVoidMethod(WalletListenerClass, callback, action, txItems);

    env->DeleteLocalRef(txItems);
}

void WalletModel::onSyncProgressUpdated(int done, int total)
{
    LOG_DEBUG() << "onSyncProgressUpdated(" << done << ", " << total << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onSyncProgressUpdated", "(II)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, done, total);
}

void WalletModel::onChangeCalculated(Amount change)
{
    LOG_DEBUG() << "onChangeCalculated(" << change << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onChangeCalculated", "(J)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, change);
}

void WalletModel::onAllUtxoChanged(const std::vector<Coin>& utxosVec)
{
    LOG_DEBUG() << "onAllUtxoChanged()";

    JNIEnv* env = Android_JNI_getEnv();

    jobjectArray utxos = convertCoinsToJObject(env, utxosVec);

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAllUtxoChanged", "([L" BEAM_JAVA_PATH "/entities/dto/UtxoDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, utxos);

    env->DeleteLocalRef(utxos);
}

void WalletModel::onAddresses(bool own, const std::vector<WalletAddress>& addresses)
{
    LOG_DEBUG() << "onAddresses(" << own << ")";

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

            env->DeleteLocalRef(addr);
        }
    }

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAddresses", "(Z[L" BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, own, addrArray);

    env->DeleteLocalRef(addrArray);
}

void WalletModel::onSwapOffersChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::SwapOffer>& offers)
{
    LOG_DEBUG() << "onSwapOffersChanged()";

    // TODO
}

void WalletModel::onGeneratedNewAddress(const WalletAddress& address)
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

    env->DeleteLocalRef(addr);
}

void WalletModel::onNewAddressFailed()
{

}

void WalletModel::onChangeCurrentWalletIDs(WalletID senderID, WalletID receiverID)
{
}

void WalletModel::onNodeConnectionChanged(bool isNodeConnected)
{
    LOG_DEBUG() << "onNodeConnectedStatusChanged(" << isNodeConnected << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeConnectedStatusChanged", "(Z)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, isNodeConnected);
}

void WalletModel::onWalletError(ErrorType error)
{
    LOG_DEBUG() << "onWalletError: error = " << static_cast<int>(error);

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeConnectionFailed", "(I)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, static_cast<int>(error));
}

void WalletModel::FailedToStartWallet()
{
    
}

void WalletModel::onSendMoneyVerified()
{
    
}

void WalletModel::onCantSendToExpired()
{
    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onCantSendToExpired", "()V");

    env->CallStaticVoidMethod(WalletListenerClass, callback);
}

void WalletModel::onPaymentProofExported(const TxID& txID, const ByteBuffer& proof)
{
    string strProof;
    strProof.resize(proof.size() * 2);

    to_hex(strProof.data(), proof.data(), proof.size());
    storage::PaymentInfo paymentInfo = storage::PaymentInfo::FromByteBuffer(proof);

    JNIEnv* env = Android_JNI_getEnv();

    jobject jPaymentInfo = env->AllocObject(PaymentInfoClass);

    {
        setStringField(env, PaymentInfoClass, jPaymentInfo, "senderId", to_string(paymentInfo.m_Sender));
        setStringField(env, PaymentInfoClass, jPaymentInfo, "receiverId", to_string(paymentInfo.m_Receiver));
        setLongField(env, PaymentInfoClass, jPaymentInfo, "amount", paymentInfo.m_Amount);
        setStringField(env, PaymentInfoClass, jPaymentInfo, "kernelId", to_string(paymentInfo.m_KernelID));
        setBooleanField(env, PaymentInfoClass, jPaymentInfo, "isValid", paymentInfo.IsValid());
        setStringField(env, PaymentInfoClass, jPaymentInfo, "rawProof", strProof);
    }

    jstring jStrTxId = env->NewStringUTF(to_hex(txID.data(), txID.size()).c_str());

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onPaymentProofExported", "(Ljava/lang/String;L" BEAM_JAVA_PATH "/entities/dto/PaymentInfoDTO;)V");

    
    //jstring jStrProof = env->NewStringUTF(str.c_str());
    env->CallStaticVoidMethod(WalletListenerClass, callback, jStrTxId, jPaymentInfo);

    env->DeleteLocalRef(jStrTxId);
    env->DeleteLocalRef(jPaymentInfo);
}

void WalletModel::onCoinsByTx(const std::vector<Coin>& coins)
{
    JNIEnv* env = Android_JNI_getEnv();

    jobjectArray utxos = convertCoinsToJObject(env, coins);

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onCoinsByTx", "([L" BEAM_JAVA_PATH "/entities/dto/UtxoDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, utxos);

    env->DeleteLocalRef(utxos);
}

void WalletModel::onAddressChecked(const std::string& addr, bool isValid)
{

}

void WalletModel::onImportRecoveryProgress(uint64_t done, uint64_t total)
{
    LOG_DEBUG() << "onImportRecoveryProgress(" << done << ", " << total << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onImportRecoveryProgress", "(JJ)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, done, total);
}