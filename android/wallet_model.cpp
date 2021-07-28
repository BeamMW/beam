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
#include "wallet/core/common_utils.h"

#include <jni.h>
#include "common.h"

using namespace beam;
using namespace beam::wallet;
using namespace beam::io;
using namespace std;

    
namespace
{
    std::string txIDToString(const TxID& txId)
    {
        return to_hex(txId.data(), txId.size());
    }   

    jobject fillNotificationInfo(JNIEnv* env, const Notification& notification)
    {
        jobject jNotificationInfo = env->AllocObject(NotificationClass);

        setStringField(env, NotificationClass, jNotificationInfo, "id", to_string(notification.m_ID));
        setIntField(env, NotificationClass, jNotificationInfo, "state", beam::underlying_cast(notification.m_state));
        setLongField(env, NotificationClass, jNotificationInfo, "createTime", notification.m_createTime);

        return jNotificationInfo;
    }

    jobject fillAddressData(JNIEnv* env, const WalletAddress& address)
    {
        jobject addr = env->AllocObject(WalletAddressClass);

        setStringField(env, WalletAddressClass, addr, "walletID", to_string(address.m_walletID));
        setStringField(env, WalletAddressClass, addr, "identity", to_string(address.m_Identity));
        setStringField(env, WalletAddressClass, addr, "label", address.m_label);
        setStringField(env, WalletAddressClass, addr, "category", address.m_category);
        setLongField(env, WalletAddressClass, addr, "createTime", address.m_createTime);
        setLongField(env, WalletAddressClass, addr, "duration", address.m_duration);
        setLongField(env, WalletAddressClass, addr, "own", address.m_OwnID);
        setStringField(env, WalletAddressClass, addr, "address", address.m_Address);

        return addr;
    }

    jobject fillTransactionData(JNIEnv* env, const TxDescription& txDescription)
    {
        jobject tx = env->AllocObject(TxDescriptionClass);

        setStringField(env, TxDescriptionClass, tx, "id", to_hex(txDescription.m_txId.data(), txDescription.m_txId.size()));
        setLongField(env, TxDescriptionClass, tx, "amount", txDescription.m_amount);
        setLongField(env, TxDescriptionClass, tx, "fee", txDescription.m_fee);
        setLongField(env, TxDescriptionClass, tx, "minHeight", txDescription.m_minHeight);

        setStringField(env, TxDescriptionClass, tx, "peerId", to_string(txDescription.m_peerId));
        setStringField(env, TxDescriptionClass, tx, "myId", to_string(txDescription.m_myId));

        setStringField(env, TxDescriptionClass, tx, "message", string(txDescription.m_message.begin(), txDescription.m_message.end()));
        setLongField(env, TxDescriptionClass, tx, "createTime", txDescription.m_createTime);
        setLongField(env, TxDescriptionClass, tx, "modifyTime", txDescription.m_modifyTime);
        setBooleanField(env, TxDescriptionClass, tx, "sender", txDescription.m_sender);
        setBooleanField(env, TxDescriptionClass, tx, "selfTx", txDescription.m_selfTx);
        setIntField(env, TxDescriptionClass, tx, "status", static_cast<jint>(txDescription.m_status));
        setStringField(env, TxDescriptionClass, tx, "kernelId", to_string(txDescription.m_kernelID));
        setIntField(env, TxDescriptionClass, tx, "failureReason", static_cast<jint>(txDescription.m_failureReason));

        setStringField(env, TxDescriptionClass, tx, "identity", txDescription.getIdentity(txDescription.m_sender));

        setStringField(env, TxDescriptionClass, tx, "senderIdentity", txDescription.getSenderIdentity());
        setStringField(env, TxDescriptionClass, tx, "receiverIdentity", txDescription.getReceiverIdentity());

        setStringField(env, TxDescriptionClass, tx, "receiverAddress", txDescription.getAddressTo());
        setStringField(env, TxDescriptionClass, tx, "senderAddress", txDescription.getAddressFrom());

        setStringField(env, TxDescriptionClass, tx, "token", txDescription.getToken());

        setIntField(env, TxDescriptionClass, tx, "assetId", txDescription.m_assetId);

        if(txDescription.m_txType == wallet::TxType::PushTransaction) {
            auto token = txDescription.getToken();
            if (token.size() > 0) { //send
                auto p = wallet::ParseParameters(token);
                
                auto voucher = p->GetParameter<ShieldedTxo::Voucher>(TxParameterID::Voucher);
                setBooleanField(env, TxDescriptionClass, tx, "isMaxPrivacy", !!voucher);

                auto vouchers = p->GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList);
                if (vouchers && !vouchers->empty())
                {
                    setBooleanField(env, TxDescriptionClass, tx, "isShielded", true);
                }
                else
                {
                    auto gen = p->GetParameter<ShieldedTxo::PublicGen>(TxParameterID::PublicAddreessGen);
                    if (gen)
                    {
                         setBooleanField(env, TxDescriptionClass, tx, "isPublicOffline", true);
                    }
                }
            }
            else { //recieved
                auto storedType = txDescription.GetParameter<TxAddressType>(TxParameterID::AddressType);
                if (storedType)
                {
                    if(storedType == TxAddressType::PublicOffline) {
                         setBooleanField(env, TxDescriptionClass, tx, "isPublicOffline", true);
                    }
                    else if(storedType == TxAddressType::MaxPrivacy) {
                         setBooleanField(env, TxDescriptionClass, tx, "isMaxPrivacy", true);
                    }
                    else if(storedType == TxAddressType::Offline) {
                           setBooleanField(env, TxDescriptionClass, tx, "isShielded", true);
                    }
                }
            }
        }

        return tx;
    }

    jobjectArray convertShieldedToJObject(JNIEnv* env, const std::vector<beam::wallet::ShieldedCoin>& utxosVec)
    {
        jobjectArray utxos = 0;

        if (!utxosVec.empty())
        {
            utxos = env->NewObjectArray(static_cast<jsize>(utxosVec.size()), UtxoClass, NULL);

            for (size_t i = 0; i < utxosVec.size(); ++i)
            {
                
            const auto& coin = utxosVec[i];
            
            jobject utxo = env->AllocObject(UtxoClass);
            
            std::string idString = std::to_string(coin.m_spentHeight);

            setLongField(env, UtxoClass, utxo, "id", coin.m_spentHeight);
            setStringField(env, UtxoClass, utxo, "stringId", idString);
            setLongField(env, UtxoClass, utxo, "amount", coin.m_CoinID.m_Value);
            setLongField(env, UtxoClass, utxo, "txoID", coin.m_TxoID);
            setBooleanField(env, UtxoClass, utxo, "isShielded", true);
            setIntField(env, UtxoClass, utxo, "assetId", coin.getAssetID());

            switch (coin.m_Status)
            {
                case ShieldedCoin::Available:
                    setIntField(env, UtxoClass, utxo, "status", 1);
                    break;
                case ShieldedCoin::Maturing:
                    setIntField(env, UtxoClass, utxo, "status", 2);
                    break;
                case ShieldedCoin::Unavailable:
                    setIntField(env, UtxoClass, utxo, "status", 0);
                    break;
                case ShieldedCoin::Outgoing:
                    setIntField(env, UtxoClass, utxo, "status", 3);
                    break;
                case ShieldedCoin::Incoming:
                    setIntField(env, UtxoClass, utxo, "status", 4);
                    break;
                case ShieldedCoin::Spent:
                    setIntField(env, UtxoClass, utxo, "status", 6);
                    break;
                default:
                    break;
            }
            

            setLongField(env, UtxoClass, utxo, "maturity", coin.m_confirmHeight);
            setIntField(env, UtxoClass, utxo, "keyType", -1);
            setLongField(env, UtxoClass, utxo, "confirmHeight", coin.m_confirmHeight);
            
            if (coin.m_createTxId)
                 setStringField(env, UtxoClass, utxo, "createTxId", to_hex(coin.m_createTxId->data(), coin.m_createTxId->size()));

            if (coin.m_spentTxId)
                setStringField(env, UtxoClass, utxo, "spentTxId", to_hex(coin.m_spentTxId->data(), coin.m_spentTxId->size()));


            
            env->SetObjectArrayElement(utxos, static_cast<jsize>(i), utxo);
            
            env->DeleteLocalRef(utxo);
            }
        }

        return utxos;
    }

    jobjectArray convertCoinsToJObject(JNIEnv* env, const std::vector<Coin>& utxosVec)
    {
        jobjectArray utxos = 0;

        if (!utxosVec.empty())
        {
            utxos = env->NewObjectArray(static_cast<jsize>(utxosVec.size()), UtxoClass, NULL);

            for (size_t i = 0; i < utxosVec.size(); ++i)
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
                setIntField(env, UtxoClass, utxo, "assetId", coin.getAssetID());
                setBooleanField(env, UtxoClass, utxo, "isShielded", false);

                if (coin.m_createTxId)
                    setStringField(env, UtxoClass, utxo, "createTxId", to_hex(coin.m_createTxId->data(), coin.m_createTxId->size()));

                if (coin.m_spentTxId)
                    setStringField(env, UtxoClass, utxo, "spentTxId", to_hex(coin.m_spentTxId->data(), coin.m_spentTxId->size()));

                env->SetObjectArrayElement(utxos, static_cast<jsize>(i), utxo);

                env->DeleteLocalRef(utxo);
            }
        }

        return utxos;
    }

    jobjectArray convertAddressesToJObject(JNIEnv* env, const std::vector<WalletAddress>& addresses)
    {
        jobjectArray addrArray = 0;

        if (!addresses.empty())
        {
            addrArray = env->NewObjectArray(static_cast<jsize>(addresses.size()), WalletAddressClass, NULL);

            for (size_t i = 0; i < addresses.size(); ++i)
            {
                const auto& addrRef = addresses[i];

                jobject addr = fillAddressData(env, addrRef);

                env->SetObjectArrayElement(addrArray, static_cast<jsize>(i), addr);

                env->DeleteLocalRef(addr);
            }
        }
        return addrArray;
    }

     jobjectArray convertExchangeRatesToJObject(JNIEnv* env, const std::vector<ExchangeRate>& rates)
     {
         jobjectArray ratesArray = 0;

         if (!rates.empty())
         {
             ratesArray = env->NewObjectArray(static_cast<jsize>(rates.size()), ExchangeRateClass, NULL);

             for (size_t i = 0; i < rates.size(); ++i)
             {
                auto rate = rates[i];
                auto m_from_name = rate.m_from.m_value;
                auto m_to_name = rate.m_to.m_value;
                auto m_rate = rate.m_rate;
                                
                jobject rateObject = env->AllocObject(ExchangeRateClass);
                    {
                        setStringField(env, ExchangeRateClass, rateObject, "fromName", m_from_name);
                        setStringField(env, ExchangeRateClass, rateObject, "toName", m_to_name);
                        setLongField(env, ExchangeRateClass, rateObject, "rate", m_rate);
                    }

                env->SetObjectArrayElement(ratesArray, static_cast<jsize>(i), rateObject);

                env->DeleteLocalRef(rateObject);
             }
         }
         return ratesArray;
     }

    void callSoftwareUpdateNotification(JNIEnv* env, const Notification& notification, ChangeAction action)
    {
        WalletImplVerInfo walletVersionInfo;

        if (fromByteBuffer(notification.m_content, walletVersionInfo))
        {
            jobject jNotificationInfo = fillNotificationInfo(env, notification);

            jobject jVersionInfo = env->AllocObject(VersionInfoClass);
            {
                setIntField(env, VersionInfoClass, jVersionInfo, "application", beam::underlying_cast(walletVersionInfo.m_application));
                setLongField(env, VersionInfoClass, jVersionInfo, "versionMajor", walletVersionInfo.m_version.m_major);
                setLongField(env, VersionInfoClass, jVersionInfo, "versionMinor", walletVersionInfo.m_version.m_minor);
                setLongField(env, VersionInfoClass, jVersionInfo, "versionRevision", walletVersionInfo.m_UIrevision);
            }

            if (walletVersionInfo.m_application == VersionInfo::Application::AndroidWallet)
            {
                jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNewVersionNotification", "(IL" BEAM_JAVA_PATH "/entities/dto/NotificationDTO;L" BEAM_JAVA_PATH "/entities/dto/VersionInfoDTO;)V");
                env->CallStaticVoidMethod(WalletListenerClass, callback, action, jNotificationInfo, jVersionInfo);
            }

            env->DeleteLocalRef(jNotificationInfo);
            env->DeleteLocalRef(jVersionInfo);
        }
    }
    
    void callAddressStatusNotification(JNIEnv* env, const Notification& notification, ChangeAction action)
    {
        WalletAddress address;

        if (fromByteBuffer(notification.m_content, address))
        {
            jobject jNotificationInfo = fillNotificationInfo(env, notification);

            jobject jAddress = fillAddressData(env, address);

            jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAddressChangedNotification", "(IL" BEAM_JAVA_PATH "/entities/dto/NotificationDTO;L" BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO;)V");
            
            env->CallStaticVoidMethod(WalletListenerClass, callback, action, jNotificationInfo, jAddress);

            env->DeleteLocalRef(jNotificationInfo);
            env->DeleteLocalRef(jAddress);
        }
    }

    void callTransactionFailed(JNIEnv* env, const Notification& notification, ChangeAction action)
    {
        TxToken token;

        if (fromByteBuffer(notification.m_content, token))
        {
            TxParameters txParameters = token.UnpackParameters();
            TxDescription txDescription(txParameters);

            jobject jNotificationInfo = fillNotificationInfo(env, notification);

            jobject jTransaction = fillTransactionData(env, txDescription);

            jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onTransactionFailedNotification", "(IL" BEAM_JAVA_PATH "/entities/dto/NotificationDTO;L" BEAM_JAVA_PATH "/entities/dto/TxDescriptionDTO;)V");
            
            env->CallStaticVoidMethod(WalletListenerClass, callback, action, jNotificationInfo, jTransaction);

            env->DeleteLocalRef(jNotificationInfo);
            env->DeleteLocalRef(jTransaction);
        }
    }

    void callTransactionCompleted(JNIEnv* env, const Notification& notification, ChangeAction action)
    {
        TxToken token;

        if (fromByteBuffer(notification.m_content, token))
        {
            TxParameters txParameters = token.UnpackParameters();
            TxDescription txDescription(txParameters);

            jobject jNotificationInfo = fillNotificationInfo(env, notification);

            jobject jTransaction = fillTransactionData(env, txDescription);

            jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onTransactionCompletedNotification", "(IL" BEAM_JAVA_PATH "/entities/dto/NotificationDTO;L" BEAM_JAVA_PATH "/entities/dto/TxDescriptionDTO;)V");
            
            env->CallStaticVoidMethod(WalletListenerClass, callback, action, jNotificationInfo, jTransaction);

            env->DeleteLocalRef(jNotificationInfo);
            env->DeleteLocalRef(jTransaction);
        }
    }

    void callBeamNewsNotification(JNIEnv* env, const Notification& notification, ChangeAction action)
    {
        // TODO: deserialize notification content and fill JAVA data object

        jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onBeamNewsNotification", "(I)V");

        env->CallStaticVoidMethod(WalletListenerClass, callback, action);
    }

    jobjectArray convertWalletStatusToJObject(JNIEnv* env, const WalletStatus& status)
    {
        auto assets = status.all;

        jobjectArray assetsArray = 0;
        assetsArray = env->NewObjectArray(static_cast<jsize>(assets.size()), WalletStatusClass, NULL);

        int i = 0;
        for (const auto& [key, value] : assets) {
            auto assetId = key;
            
            LOG_DEBUG() << "convertWalletStatusToJObject(" << assetId << ")";

            jobject walletStatus = env->AllocObject(WalletStatusClass);
            setLongField(env, WalletStatusClass, walletStatus, "available", AmountBig::get_Lo(value.available));
            setLongField(env, WalletStatusClass, walletStatus, "receiving", AmountBig::get_Lo(value.receiving));
            setLongField(env, WalletStatusClass, walletStatus, "sending",   AmountBig::get_Lo(value.sending));
            setLongField(env, WalletStatusClass, walletStatus, "maturing",  AmountBig::get_Lo(value.maturing));
            setLongField(env, WalletStatusClass, walletStatus, "shielded",  AmountBig::get_Lo(value.shielded));
            setLongField(env, WalletStatusClass, walletStatus, "maxPrivacy", AmountBig::get_Lo(value.maturingMP));
            setIntField(env, WalletStatusClass, walletStatus, "assetId", static_cast<jint>(assetId));

            jobject systemState = env->AllocObject(SystemStateClass);

            setLongField(env, SystemStateClass, systemState, "height", status.stateID.m_Height);
            setStringField(env, SystemStateClass, systemState, "hash", to_hex(status.stateID.m_Hash.m_pData, status.stateID.m_Hash.nBytes));

            jfieldID systemStateID = env->GetFieldID(WalletStatusClass, "system", "L" BEAM_JAVA_PATH "/entities/dto/SystemStateDTO;");
            env->SetObjectField(walletStatus, systemStateID, systemState);


            env->SetObjectArrayElement(assetsArray, static_cast<jsize>(i), walletStatus);
            env->DeleteLocalRef(walletStatus);
            env->DeleteLocalRef(systemState);

            i = i + 1;
        }

        return assetsArray;
     }
}

WalletModel::WalletModel(IWalletDB::Ptr walletDB, const std::string& nodeAddr, Reactor::Ptr reactor)
    : WalletClient(Rules::get() , walletDB, nodeAddr, reactor)
{    
}

WalletModel::~WalletModel()
{
    stopReactor();
}

void WalletModel::onStatus(const WalletStatus& status)
{
    LOG_DEBUG() << "onStatus()";

    JNIEnv* env = Android_JNI_getEnv();
    jobjectArray jStatus = convertWalletStatusToJObject(env, status);

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onStatus", "([L" BEAM_JAVA_PATH "/entities/dto/WalletStatusDTO;)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, jStatus);

    env->DeleteLocalRef(jStatus);
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

        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];

            jobject tx = fillTransactionData(env, item);

            env->SetObjectArrayElement(txItems, static_cast<jsize>(i), tx);

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

void WalletModel::onChangeCalculated(beam::Amount changeAsset, beam::Amount changeBeam, beam::Asset::ID assetId)
{
    LOG_DEBUG() << "onChangeCalculated(" << changeBeam << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onChangeCalculated", "(J)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, changeAsset, changeBeam, assetId);
}

void WalletModel::onCoinsSelected(const CoinsSelectionInfo& selectionRes)
{
    LOG_DEBUG() << "onCoinsSelected(" << selectionRes.m_explicitFee << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onCoinsSelected", "(JJJ)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, 
                                                    selectionRes.m_explicitFee, 
                                                    selectionRes.m_changeAsset, 
                                                    selectionRes.m_minimalExplicitFee);
}

void WalletModel::onNormalCoinsChanged(ChangeAction action, const std::vector<Coin>& utxosVec)
{
    LOG_DEBUG() << "onNormalCoinsChanged()";

    JNIEnv* env = Android_JNI_getEnv();

    jobjectArray utxos = convertCoinsToJObject(env, utxosVec);

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNormalUtxoChanged", "(I[L" BEAM_JAVA_PATH "/entities/dto/UtxoDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, action, utxos);

    env->DeleteLocalRef(utxos);
}

void WalletModel::onAddressesChanged(ChangeAction action, const std::vector<WalletAddress>& addresses)
{
    LOG_DEBUG() << "onAddressesChanged()";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAddressesChanged", "(I[L" BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO;)V");

    jobjectArray addrArray = convertAddressesToJObject(env, addresses);

    env->CallStaticVoidMethod(WalletListenerClass, callback, action, addrArray);

    env->DeleteLocalRef(addrArray);
}

void WalletModel::onAddresses(bool own, const std::vector<WalletAddress>& addresses)
{
    LOG_DEBUG() << "onAddresses(" << own << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jobjectArray addrArray = convertAddressesToJObject(env, addresses);

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAddresses", "(Z[L" BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, own, addrArray);

    env->DeleteLocalRef(addrArray);
}

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
void WalletModel::onSwapOffersChanged(ChangeAction action, const std::vector<SwapOffer>& offers)
{
    LOG_DEBUG() << "onSwapOffersChanged()";

    // TODO
}
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

void WalletModel::onGeneratedNewAddress(const WalletAddress& address)
{
    LOG_DEBUG() << "onGeneratedNewAddress()";

    JNIEnv* env = Android_JNI_getEnv();

    jobject addr = fillAddressData(env, address);

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onGeneratedNewAddress", "(L" BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, addr);
    env->DeleteLocalRef(addr);
}

void WalletModel::onSwapParamsLoaded(const beam::ByteBuffer& params)
{
    LOG_DEBUG() << "onSwapParamsLoaded()";

    // TODO
}

void WalletModel::onNewAddressFailed()
{

}

void WalletModel::onNodeConnectionChanged(bool isNodeConnected)
{
    LOG_DEBUG() << "onNodeConnectedStatusChanged(" << isNodeConnected << ")";

    if(isNodeConnected) 
    {
        auto trusted = this->isConnectionTrusted();

        LOG_DEBUG() << "isConnectionTrustedCheck()" << trusted;
    }

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeConnectedStatusChanged", "(Z)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, isNodeConnected);    
}

void WalletModel::onWalletError(ErrorType error)
{
    LOG_DEBUG() << "onWalletError: error = " << underlying_cast(error);

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeConnectionFailed", "(I)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, underlying_cast(error));
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
   
    JNIEnv* env = Android_JNI_getEnv();

    try
    {
        storage::PaymentInfo paymentInfo = storage::PaymentInfo::FromByteBuffer(proof);

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

        env->CallStaticVoidMethod(WalletListenerClass, callback, jStrTxId, jPaymentInfo);

        env->DeleteLocalRef(jStrTxId);
        env->DeleteLocalRef(jPaymentInfo);
    }
    catch (...)
    {
        
    }

    try
    {
        auto shieldedPaymentInfo = beam::wallet::storage::ShieldedPaymentInfo::FromByteBuffer(proof);
       
        jobject jPaymentInfo = env->AllocObject(PaymentInfoClass);
        {   
        setStringField(env, PaymentInfoClass, jPaymentInfo, "senderId", to_string(shieldedPaymentInfo.m_Sender));
        setStringField(env, PaymentInfoClass, jPaymentInfo, "receiverId", to_string(shieldedPaymentInfo.m_Receiver));
        setLongField(env, PaymentInfoClass, jPaymentInfo, "amount", shieldedPaymentInfo.m_Amount);
        setStringField(env, PaymentInfoClass, jPaymentInfo, "kernelId", to_string(shieldedPaymentInfo.m_KernelID));
        setBooleanField(env, PaymentInfoClass, jPaymentInfo, "isValid", shieldedPaymentInfo.IsValid());
        setStringField(env, PaymentInfoClass, jPaymentInfo, "rawProof", strProof);
        }

        jstring jStrTxId = env->NewStringUTF(to_hex(txID.data(), txID.size()).c_str());

        jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onPaymentProofExported", "(Ljava/lang/String;L" BEAM_JAVA_PATH "/entities/dto/PaymentInfoDTO;)V");

        env->CallStaticVoidMethod(WalletListenerClass, callback, jStrTxId, jPaymentInfo);

        env->DeleteLocalRef(jStrTxId);
        env->DeleteLocalRef(jPaymentInfo);
    }
    catch (...)
    {
    }
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

void WalletModel::onImportDataFromJson(bool isOk)
{
    LOG_DEBUG() << "onImportDataFromJson(" << isOk << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onImportDataFromJson", "(Z)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, isOk);
}

void WalletModel::onExportDataToJson(const std::string& data)
{
    LOG_DEBUG() << "onExportDataToJson";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onExportDataToJson", "(Ljava/lang/String;)V");

    jstring jdata = env->NewStringUTF(data.c_str());

    env->CallStaticVoidMethod(WalletListenerClass, callback, jdata);
    env->DeleteLocalRef(jdata);
}

void WalletModel::onNotificationsChanged(ChangeAction action, const std::vector<Notification>& notifications)
{
    LOG_DEBUG() << "onNotificationsChanged";

    JNIEnv* env = Android_JNI_getEnv();

    for (const auto& notification : notifications)
    {
        switch(notification.m_type)
        {
            case Notification::Type::SoftwareUpdateAvailable:
                break;
            case Notification::Type::WalletImplUpdateAvailable:
                callSoftwareUpdateNotification(env, notification, action);
                break;
            case Notification::Type::AddressStatusChanged:
                callAddressStatusNotification(env, notification, action);
                break;
            case Notification::Type::TransactionFailed:
                callTransactionFailed(env, notification, action);
                break;
            case Notification::Type::TransactionCompleted:
                callTransactionCompleted(env, notification, action);
                break;
            case Notification::Type::BeamNews:
                callBeamNewsNotification(env, notification, action);
                break;
            default:
                break;
        }
    }
}

void WalletModel::onExchangeRates(const std::vector<ExchangeRate>& rates)
{
    LOG_DEBUG() << "onExchangeRates(" << rates.size() << ")";

    JNIEnv* env = Android_JNI_getEnv();
    jobjectArray jRates = convertExchangeRatesToJObject(env, rates);

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onExchangeRates", "([L" BEAM_JAVA_PATH "/entities/dto/ExchangeRateDTO;)V");

     env->CallStaticVoidMethod(WalletListenerClass, callback, jRates);

     env->DeleteLocalRef(jRates);
}

void WalletModel::onGetAddress(const beam::wallet::WalletID& addr, const boost::optional<beam::wallet::WalletAddress>& address, size_t offlinePayments)
{
    int convertdata = static_cast<int>(offlinePayments);

    LOG_DEBUG() << "onGetAddress(" << convertdata << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onGetAddress", "(I)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, convertdata);
}


void WalletModel::onShieldedCoinChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::ShieldedCoin>& items) 
{
    LOG_DEBUG() << "onShieldedCoinChanged()";

    JNIEnv* env = Android_JNI_getEnv();

    jobjectArray utxos = convertShieldedToJObject(env, items);

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAllShieldedUtxoChanged", "(I[L" BEAM_JAVA_PATH "/entities/dto/UtxoDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, action, utxos);

    env->DeleteLocalRef(utxos);
}

void WalletModel::onPostFunctionToClientContext(MessageFunction&& func) {
    LOG_DEBUG() << "onPostFunctionToClientContext()";
    
    doFunction(func);
}

void WalletModel::callMyFunction()
{
    myFunction();
}

void WalletModel::doFunction(const std::function<void()>& func)
{
    func();  
}

void WalletModel::onExportTxHistoryToCsv(const std::string& data) 
{
    LOG_DEBUG() << "onExportTxHistoryToCsv()";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onExportTxHistoryToCsv", "(Ljava/lang/String;)V");

    jstring jdata = env->NewStringUTF(data.c_str());

    env->CallStaticVoidMethod(WalletListenerClass, callback, jdata);
    env->DeleteLocalRef(jdata);
}

void WalletModel::onPublicAddress(const std::string& publicAddr)
{
    LOG_DEBUG() << "onPublicAddress()";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onPublicAddress", "(Ljava/lang/String;)V");

    jstring jdata = env->NewStringUTF(publicAddr.c_str());

    env->CallStaticVoidMethod(WalletListenerClass, callback, jdata);
    env->DeleteLocalRef(jdata);
}

void WalletModel::onAssetInfo(Asset::ID assetId, const WalletAsset& asset) 
{
    auto info = WalletAssetMeta(asset);

    JNIEnv* env = Android_JNI_getEnv();

    jobject jAssetInfo = env->AllocObject(AssetInfoClass);
    {   
        setStringField(env, AssetInfoClass, jAssetInfo, "unitName", info.GetUnitName());
        setStringField(env, AssetInfoClass, jAssetInfo, "nthUnitName", info.GetNthUnitName());
        setStringField(env, AssetInfoClass, jAssetInfo, "shortName", info.GetShortName());
        setStringField(env, AssetInfoClass, jAssetInfo, "shortDesc", info.GetShortDesc());
        setStringField(env, AssetInfoClass, jAssetInfo, "longDesc", info.GetLongDesc());
        setStringField(env, AssetInfoClass, jAssetInfo, "name", info.GetName());
        setStringField(env, AssetInfoClass, jAssetInfo, "site", info.GetSiteUrl());
        setStringField(env, AssetInfoClass, jAssetInfo, "paper", info.GetPaperUrl());
        setIntField(env, AssetInfoClass, jAssetInfo, "id", static_cast<jint>(assetId));
    }

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onAssetInfo", "(L" BEAM_JAVA_PATH "/entities/dto/AssetInfoDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, jAssetInfo);
    env->DeleteLocalRef(jAssetInfo);
}


