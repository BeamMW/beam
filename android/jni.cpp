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

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"
#include "wallet/wallet_model_async.h"
#include "wallet/default_peers.h"

#include "utility/bridge.h"
#include "utility/string_helpers.h"
#include "mnemonic/mnemonic.h"

#include <boost/filesystem.hpp>
#include <jni.h>

#include "common.h"
#include "wallet_model.h"
#include "node_model.h"
#include "version.h"

#define WALLET_FILENAME "wallet.db"
#define BBS_FILENAME "keys.bbs"

using namespace beam;
using namespace beam::io;
using namespace std;

namespace fs = boost::filesystem;

namespace
{
    string to_string(const beam::WalletID& id)
    {
        static_assert(sizeof(id) == sizeof(id.m_Channel) + sizeof(id.m_Pk), "");
        return beam::to_hex(&id, sizeof(id));
    }

    static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours

    template<typename Observer, typename Notifier>
    struct ScopedSubscriber
    {
        ScopedSubscriber(Observer* observer, const std::shared_ptr<Notifier>& notifier)
            : m_observer(observer)
            , m_notifier(notifier)
        {
            m_notifier->subscribe(m_observer);
        }

        ~ScopedSubscriber()
        {
            m_notifier->unsubscribe(m_observer);
        }
    private:
        Observer * m_observer;
        std::shared_ptr<Notifier> m_notifier;
    };

    using WalletSubscriber = ScopedSubscriber<IWalletObserver, beam::Wallet>;

    static unique_ptr<WalletModel> walletModel;

    void initLogger(const string& appData)
    {
        static auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, "wallet_", (fs::path(appData) / fs::path("logs")).string());

        Rules::get().UpdateChecksum();
        LOG_INFO() << "Beam Mobile Wallet " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
        LOG_INFO() << "Rules signature: " << Rules::get().Checksum;
    }
}


#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jobject JNICALL BEAM_JAVA_API_INTERFACE(createWallet)(JNIEnv *env, jobject thiz, 
    jstring nodeAddrStr, jstring appDataStr, jstring passStr, jstring phrasesStr)
{
    auto appData = JString(env, appDataStr).value();

    initLogger(appData);
    
    LOG_DEBUG() << "creating wallet...";

    auto pass = JString(env, passStr).value();

    SecString seed;
    
    {

        WordList phrases = string_helpers::split(JString(env, phrasesStr).value(), ';');
        assert(phrases.size() == 12);
        if (!isValidMnemonic(phrases, language::en))
        {
            LOG_ERROR() << "Invalid seed phrases provided: " << JString(env, phrasesStr).value();
            return nullptr;
        }

        auto buf = decodeMnemonic(phrases);
        seed.assign(buf.data(), buf.size());
    }

    auto walletDB = WalletDB::init(
        appData + "/" WALLET_FILENAME,
        pass,
        seed.hash());

    if(walletDB)
    {
        LOG_DEBUG() << "wallet successfully created.";

        // generate default address

        WalletAddress address = wallet::createAddress(*walletDB);
        address.m_label = "default";
        walletDB->saveAddress(address);

        walletModel = make_unique<WalletModel>(walletDB, JString(env, nodeAddrStr).value());

        jobject walletObj = env->AllocObject(WalletClass);

        walletModel->start();

        return walletObj;
    }

    LOG_ERROR() << "wallet creation error.";

    return nullptr;
}

JNIEXPORT jboolean JNICALL BEAM_JAVA_API_INTERFACE(isWalletInitialized)(JNIEnv *env, jobject thiz, 
    jstring appData)
{
    LOG_DEBUG() << "checking if wallet exists...";

    return WalletDB::isInitialized(JString(env, appData).value() + "/" WALLET_FILENAME) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL BEAM_JAVA_API_INTERFACE(closeWallet)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "close wallet if it exists";

    if (walletModel)
    {
        walletModel.reset();
    }
}

JNIEXPORT jboolean JNICALL BEAM_JAVA_API_INTERFACE(isWalletRunning)(JNIEnv *env, jobject thiz)
{
    return walletModel != nullptr;
}

JNIEXPORT jobject JNICALL BEAM_JAVA_API_INTERFACE(openWallet)(JNIEnv *env, jobject thiz, 
    jstring nodeAddrStr, jstring appDataStr, jstring passStr)
{
    auto appData = JString(env, appDataStr).value();

    initLogger(appData);

    LOG_DEBUG() << "opening wallet...";

    string pass = JString(env, passStr).value();
    auto walletDB = WalletDB::open(appData + "/" WALLET_FILENAME, pass);

    if(walletDB)
    {
        LOG_DEBUG() << "wallet successfully opened.";

        walletModel = make_unique<WalletModel>(walletDB, JString(env, nodeAddrStr).value());

        jobject walletObj = env->AllocObject(WalletClass);

        walletModel->start();

        return walletObj;
    }

    LOG_ERROR() << "wallet not opened.";

    return nullptr;
}

JNIEXPORT jobject JNICALL BEAM_JAVA_API_INTERFACE(createMnemonic)(JNIEnv *env, jobject thiz)
{
    auto phrases = beam::createMnemonic(beam::getEntropy(), beam::language::en);

    jobjectArray phrasesArray = env->NewObjectArray(static_cast<jsize>(phrases.size()), env->FindClass("java/lang/String"), 0);

    int i = 0;
    for (auto& phrase : phrases)
    {
        jstring str = env->NewStringUTF(phrase.c_str());
        env->SetObjectArrayElement(phrasesArray, i++, str);
        env->DeleteLocalRef(str);
    }

    return phrasesArray;
}

JNIEXPORT jobject JNICALL BEAM_JAVA_API_INTERFACE(getDefaultPeers)(JNIEnv *env, jobject thiz)
{
    auto peers = beam::getDefaultPeers();

    jobjectArray peersArray = env->NewObjectArray(static_cast<jsize>(peers.size()), env->FindClass("java/lang/String"), 0);

    int i = 0;
    for (auto& peer : peers)
    {
        jstring str = env->NewStringUTF(peer.c_str());
        env->SetObjectArrayElement(peersArray, i++, str);
        env->DeleteLocalRef(str);
    }

    return peersArray;
}

JNIEXPORT jboolean JNICALL BEAM_JAVA_API_INTERFACE(checkReceiverAddress)(JNIEnv *env, jobject thiz, jstring address)
{
    auto str = JString(env, address).value();

    return beam::check_receiver_address(str);
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(getWalletStatus)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "getWalletStatus()";

    walletModel->getAsync()->getWalletStatus();
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(getUtxosStatus)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "getUtxosStatus()";

    walletModel->getAsync()->getUtxosStatus();
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(syncWithNode)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "syncWithNode()";

    walletModel->getAsync()->syncWithNode();
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(sendMoney)(JNIEnv *env, jobject thiz,
    jstring receiverAddr, jstring comment, jlong amount, jlong fee)
{
    LOG_DEBUG() << "sendMoney(" << JString(env, receiverAddr).value() << ", " << JString(env, comment).value() << ", " << amount << ", " << fee << ")";

    WalletID walletID(Zero);
    walletID.FromHex(JString(env, receiverAddr).value());

    // TODO: show 'operation in process' animation here?
    walletModel->getAsync()->sendMoney(walletID
        , JString(env, comment).value()
        , beam::Amount(amount)
        , beam::Amount(fee));
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(calcChange)(JNIEnv *env, jobject thiz,
    jlong amount)
{
    LOG_DEBUG() << "calcChange(" << amount << ")";

    walletModel->getAsync()->calcChange(beam::Amount(amount));
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(getAddresses)(JNIEnv *env, jobject thiz,
    jboolean own)
{
    LOG_DEBUG() << "getAddresses(" << own << ")";

    walletModel->getAsync()->getAddresses(own);
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(generateNewAddress)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "generateNewAddress()";

    walletModel->getAsync()->generateNewAddress();
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(saveAddress)(JNIEnv *env, jobject thiz,
    jobject walletAddrObj, jboolean own)
{
    LOG_DEBUG() << "saveAddress()";

    WalletAddress addr;

    addr.m_walletID.FromHex(getStringField(env, WalletAddressClass, walletAddrObj, "walletID"));
    addr.m_label = getStringField(env, WalletAddressClass, walletAddrObj, "label");
    addr.m_category = getStringField(env, WalletAddressClass, walletAddrObj, "category");
    addr.m_createTime = getLongField(env, WalletAddressClass, walletAddrObj, "createTime");
    addr.m_duration = getLongField(env, WalletAddressClass, walletAddrObj, "duration");
    addr.m_OwnID = getLongField(env, WalletAddressClass, walletAddrObj, "own");

    walletModel->getAsync()->saveAddress(addr, own);
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(saveAddressChanges)(JNIEnv *env, jobject thiz,
    jstring addr, jstring name, jboolean isNever, jboolean makeActive, jboolean makeExpired)
{
    WalletID walletID(Zero);

    if (!walletID.FromHex(JString(env, addr).value()))
    {
        LOG_ERROR() << "Address is not valid!!!";

        return;
    }
    walletModel->getAsync()->saveAddressChanges(walletID, JString(env, name).value(), isNever, makeActive, makeExpired);
}

// don't use it. i don't check it
JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(cancelTx)(JNIEnv *env, jobject thiz,
    jstring txId)
{
    LOG_DEBUG() << "cancelTx()";

    auto buffer = from_hex(JString(env, txId).value());
    TxID id;

    std::copy_n(buffer.begin(), id.size(), id.begin());
    walletModel->getAsync()->cancelTx(id);
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(deleteTx)(JNIEnv *env, jobject thiz,
    jstring txId)
{
    LOG_DEBUG() << "deleteTx()";

    auto buffer = from_hex(JString(env, txId).value());
    TxID id;

    std::copy_n(buffer.begin(), id.size(), id.begin());
    walletModel->getAsync()->deleteTx(id);
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(deleteAddress)(JNIEnv *env, jobject thiz,
    jstring walletID)
{
    WalletID id(Zero);

    if (!id.FromHex(JString(env, walletID).value()))
    {
        LOG_ERROR() << "Address is not valid!!!";

        return;
    }
    walletModel->getAsync()->deleteAddress(id);
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(changeWalletPassword)(JNIEnv *env, jobject thiz,
    jstring password)
{
    walletModel->getAsync()->changeWalletPassword(JString(env, password).value());
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv *env;
    JVM = vm;

    JVM->GetEnv((void**) &env, JNI_VERSION_1_6);

    Android_JNI_getEnv();

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/listeners/WalletListener");
        WalletListenerClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/Wallet");
        WalletClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/WalletStatusDTO");
        WalletStatusClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/SystemStateDTO");
        SystemStateClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/TxDescriptionDTO");
        TxDescriptionClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/UtxoDTO");
        UtxoClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO");
        WalletAddressClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
    }

    return JNI_VERSION_1_6;
}

#ifdef __cplusplus
}
#endif