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

#include "utility/bridge.h"
#include "utility/string_helpers.h"
#include "mnemonic/mnemonic.h"

#include <boost/filesystem.hpp>
#include <jni.h>

#define CONCAT1(prefix, class, function)    CONCAT2(prefix, class, function)
#define CONCAT2(prefix, class, function)    Java_ ## prefix ## _ ## class ## _ ## function

#define DEF2STR2(x) #x
#define DEF2STR(x) DEF2STR2(x)

#define BEAM_JAVA_PACKAGE(sep)                     com ## sep ## mw ## sep ## beam ## sep ## beamwallet ## sep ## core
#define BEAM_JAVA_PREFIX                         BEAM_JAVA_PACKAGE(_)
#define BEAM_JAVA_PATH                             "com/mw/beam/beamwallet/core" // doesn't work on clang DEF2STR(BEAM_JAVA_PACKAGE(/))
#define BEAM_JAVA_API_INTERFACE(function)         CONCAT1(BEAM_JAVA_PREFIX, Api, function)
#define BEAM_JAVA_WALLET_INTERFACE(function)     CONCAT1(BEAM_JAVA_PREFIX, entities_Wallet, function)

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

    struct JString
    {
        JString(JNIEnv *envVal, jstring nameVal)
            : env(envVal)
            , name(nameVal)
            , isCopy(JNI_FALSE)
            , data(env->GetStringUTFChars(name, &isCopy))
        {
        }

        ~JString()
        {
            if (isCopy == JNI_TRUE)
            {
                env->ReleaseStringUTFChars(name, data);
            }
        }

        string value() const
        {
            return data;
        }
    private:
        JNIEnv* env;
        jstring name;
        jboolean isCopy;
        const char* data;
    };

    //////////////
    inline void setByteField(JNIEnv *env, jclass clazz, jobject obj, const char* name, jbyte value)
    {
        env->SetByteField(obj, env->GetFieldID(clazz, name, "B"), value);
    }

    inline void setLongField(JNIEnv *env, jclass clazz, jobject obj, const char* name, jlong value)
    {
        env->SetLongField(obj, env->GetFieldID(clazz, name, "J"), value);
    }

    inline void setIntField(JNIEnv *env, jclass clazz, jobject obj, const char* name, jint value)
    {
        env->SetIntField(obj, env->GetFieldID(clazz, name, "I"), value);
    }

    inline void setBooleanField(JNIEnv *env, jclass clazz, jobject obj, const char* name, jboolean value)
    {
        env->SetBooleanField(obj, env->GetFieldID(clazz, name, "Z"), value);
    }

    inline void setStringField(JNIEnv *env, jclass clazz, jobject obj, const char* name, const std::string& value)
    {
        jfieldID fieldId = env->GetFieldID(clazz, name, "Ljava/lang/String;");
        env->SetObjectField(obj, fieldId, env->NewStringUTF(value.c_str()));
    }

    template <typename T>
    inline void setByteArrayField(JNIEnv *env, jclass clazz, jobject obj, const char* name, const T& value)
    {
        if (value.size())
        {
            jbyteArray hash = env->NewByteArray(static_cast<jsize>(value.size()));
            jbyte* hashBytes = env->GetByteArrayElements(hash, NULL);

            memcpy(hashBytes, &value[0], value.size());

            env->SetObjectField(obj, env->GetFieldID(clazz, name, "[B"), hash);

            env->ReleaseByteArrayElements(hash, hashBytes, 0);
        }
    }

    template <>
    inline void setByteArrayField<ECC::uintBig>(JNIEnv *env, jclass clazz, jobject obj, const char* name, const ECC::uintBig& value)
    {
        vector<uint8_t> data;
        data.assign(value.m_pData, value.m_pData + ECC::uintBig::nBytes);
        setByteArrayField(env, clazz, obj, name, data);
    }

    inline std::string getStringField(JNIEnv *env, jclass clazz, jobject obj, const char* name)
    {
        jfieldID fieldId = env->GetFieldID(clazz, name, "Ljava/lang/String;");

        return JString(env, (jstring)env->GetObjectField(obj, fieldId)).value();
    }

    inline jlong getLongField(JNIEnv *env, jclass clazz, jobject obj, const char* name)
    {
        jfieldID fieldId = env->GetFieldID(clazz, name, "J");

        return env->GetLongField(obj, fieldId);
    }

    inline jboolean getBooleanField(JNIEnv *env, jclass clazz, jobject obj, const char* name)
    {
        jfieldID fieldId = env->GetFieldID(clazz, name, "Z");

        return env->GetBooleanField(obj, fieldId);
    }

    inline jsize getByteArrayField(JNIEnv *env, jclass clazz, jobject obj, const char* name, uint8_t* value)
    {
        jfieldID fieldId = env->GetFieldID(clazz, name, "[B");
        jbyteArray byteArray = (jbyteArray)env->GetObjectField(obj, fieldId);

        jbyte* data = env->GetByteArrayElements(byteArray, NULL);

        if (data)
        {
            jsize size = env->GetArrayLength(byteArray);

            if (size > 0)
            {
                memcpy(value, data, size);
            }

            env->ReleaseByteArrayElements(byteArray, data, JNI_ABORT);

            return size;
        }

        return 0;
    }

    struct WalletStatus
    {
        beam::Amount available;
        beam::Amount received;
        beam::Amount sent;
        beam::Amount unconfirmed;
        struct
        {
            beam::Timestamp lastTime;
            int done;
            int total;
        } update;

        beam::Block::SystemState::ID stateID;
    };

    struct WalletModelBridge : public Bridge<IWalletModelAsync>
    {
        BRIDGE_INIT(WalletModelBridge);

        void sendMoney(const beam::WalletID& receiverId, const std::string& comment, beam::Amount&& amount, beam::Amount&& fee) override
        {
            tx.send([receiverId, comment, amount{ move(amount) }, fee{ move(fee) }](BridgeInterface& receiver_) mutable
            {
                receiver_.sendMoney(receiverId, comment, move(amount), move(fee));
            });
        }

        void syncWithNode() override
        {
            tx.send([](BridgeInterface& receiver_) mutable
            {
                receiver_.syncWithNode();
            });
        }

        void calcChange(beam::Amount&& amount) override
        {
            tx.send([amount{move(amount)}](BridgeInterface& receiver_) mutable
            {
                receiver_.calcChange(move(amount));
            });
        }

        void getWalletStatus() override
        {
            tx.send([](BridgeInterface& receiver_) mutable
            {
                receiver_.getWalletStatus();
            });
        }

        void getUtxosStatus() override
        {
            tx.send([](BridgeInterface& receiver_) mutable
            {
                receiver_.getUtxosStatus();
            });
        }

        void getAddresses(bool own) override
        {
            tx.send([own](BridgeInterface& receiver_) mutable
            {
                receiver_.getAddresses(own);
            });
        }

        void cancelTx(const beam::TxID& id) override
        {
            tx.send([id](BridgeInterface& receiver_) mutable
            {
                receiver_.cancelTx(id);
            });
        }

        void deleteTx(const beam::TxID& id) override
        {
            tx.send([id](BridgeInterface& receiver_) mutable
            {
                receiver_.deleteTx(id);
            });
        }

        void generateNewAddress() override
        {
            tx.send([](BridgeInterface& receiver_) mutable
            {
                receiver_.generateNewAddress();
            });
        }

        void saveAddress(const beam::WalletAddress& address, bool bOwn) override
        {
            tx.send([address, bOwn](BridgeInterface& receiver_) mutable
            {
                receiver_.saveAddress(address, bOwn);
            });
        }

        void changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID) override
        {
            tx.send([senderID, receiverID](BridgeInterface& receiver_) mutable
            {
                receiver_.changeCurrentWalletIDs(senderID, receiverID);
            });
        }

        void deleteAddress(const beam::WalletID& id) override
        {
            tx.send([id](BridgeInterface& receiver_) mutable
            {
                receiver_.deleteAddress(id);
            });
        }

        void saveAddressChanges(const beam::WalletID& id, const std::string& name, bool isNever, bool makeActive, bool makeExpired) override {}

        void setNodeAddress(const std::string& addr) override
        {
            tx.send([addr](BridgeInterface& receiver_) mutable
            {
                receiver_.setNodeAddress(addr);
            });
        }

        void changeWalletPassword(const SecString& pass) override
        {
            // TODO: should be investigated, don't know how to "move" SecString into lambda
            std::string passStr(pass.data(), pass.size());

            tx.send([passStr](BridgeInterface& receiver_) mutable
            {
                receiver_.changeWalletPassword(passStr);
            });
        }

        void getNetworkStatus() override {}

        void refresh() override {}
    };

    static JavaVM* JVM = NULL;

    static jclass WalletListenerClass = 0;
    static jclass WalletClass = 0;
    static jclass WalletStatusClass = 0;
    static jclass SystemStateClass = 0;
    static jclass TxDescriptionClass = 0;
    static jclass UtxoClass = 0;
    static jclass WalletAddressClass = 0;

    static JNIEnv* Android_JNI_getEnv(void)
    {
        JNIEnv *env;
#if defined (__ANDROID__)
        JVM->AttachCurrentThread(&env, NULL);
#else
        JVM->AttachCurrentThread((void**)&env, NULL);
#endif

        return env;
    }

    struct WalletModel : IWalletModelAsync, IWalletObserver
    {
        WalletModel()
        {
            _startMutex = make_shared<mutex>();
            _startCV = make_shared<condition_variable>();
        }

        void start(const string& nodeAddr, IWalletDB::Ptr WalletDB)
        {
            _thread = make_shared<thread>(&WalletModel::run, this, nodeAddr, WalletDB);

            {
                unique_lock<mutex> lock(*_startMutex);
                _startCV->wait(lock);
            }
        }

        void run(const string& nodeURI, IWalletDB::Ptr walletDB)
        {
            try
            {
                _walletDB = walletDB;

                std::unique_ptr<WalletSubscriber> wallet_subscriber;

                _reactor = Reactor::create();
                io::Reactor::Scope scope(*_reactor);

                io::Reactor::GracefulIntHandler gih(*_reactor);

                async = make_shared<WalletModelBridge>(*(static_cast<IWalletModelAsync*>(this)), *_reactor);

                //emit onStatus(getStatus());
                //emit onTxStatus(beam::ChangeAction::Reset, _walletDB->getTxHistory());

                _logRotateTimer = io::Timer::create(*_reactor);
                _logRotateTimer->start(
                    LOG_ROTATION_PERIOD, true,
                    []() {
                    Logger::get()->rotate();
                });

                auto wallet = make_shared<Wallet>(_walletDB);
                _wallet = wallet;

                struct MyNodeNetwork :public proto::FlyClient::NetworkStd {

                    MyNodeNetwork(proto::FlyClient& fc, WalletModel& wm)
                        :proto::FlyClient::NetworkStd(fc)
                        , m_This(wm)
                    {
                    }

                    WalletModel& m_This;

                    void OnNodeConnected(size_t, bool bConnected) override {
                        m_This.onNodeConnectedStatusChanged(bConnected);
                    }

                    void OnConnectionFailed(size_t, const proto::NodeConnection::DisconnectReason&) override {
                        m_This.onNodeConnectionFailed();
                    }
                };

                auto nnet = make_shared<MyNodeNetwork>(*wallet, *this);

                Address node_addr;
                if (node_addr.resolve(nodeURI.c_str()))
                {
                    nnet->m_Cfg.m_vNodes.push_back(node_addr);
                }
                else
                {
                    LOG_ERROR() << "Unable to resolve node address: " << nodeURI;
                }

                nnet->Connect();
                _nodeNetwork = nnet;

                auto wnet = make_shared<WalletNetworkViaBbs>(*wallet, *nnet, _walletDB);
                _walletNetwork = wnet;
                wallet->set_Network(*nnet, *wnet);

                wallet_subscriber = make_unique<WalletSubscriber>(static_cast<IWalletObserver*>(this), wallet);

                {
                    unique_lock<mutex> lock(*_startMutex);
                    _startCV->notify_one();
                }

                _reactor->run();
            }
            catch (const runtime_error& ex)
            {
                LOG_ERROR() << ex.what();
            }
            catch (...)
            {
                LOG_ERROR() << "Unhandled exception";
            }
        }

        void onNodeConnectedStatusChanged(bool isNodeConnected)
        {
            LOG_DEBUG() << "onNodeConnectedStatusChanged(" << isNodeConnected << ")";

            JNIEnv* env = Android_JNI_getEnv();

            jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeConnectedStatusChanged", "(Z)V");

            env->CallStaticVoidMethod(WalletListenerClass, callback, isNodeConnected);

        }

        void onNodeConnectionFailed()
        {
            LOG_DEBUG() << "onNodeConnectionFailed()";

            JNIEnv* env = Android_JNI_getEnv();

            jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeConnectionFailed", "()V");

            env->CallStaticVoidMethod(WalletListenerClass, callback);
        }

        void onGeneratedNewAddress(const WalletAddress& address)
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

        ///////////////////////////////////////////////
        // IWalletModelAsync impl
        ///////////////////////////////////////////////
        void saveAddress(const WalletAddress& address, bool bOwn) override
        {
            _walletDB->saveAddress(address);

            if (bOwn)
            {
                auto s = _walletNetwork.lock();
                if (s)
                {
                    static_pointer_cast<WalletNetworkViaBbs>(s)->AddOwnAddress(address);
                }
            }
        }

        void sendMoney(const beam::WalletID& receiver, const std::string& comment, beam::Amount&& amount, beam::Amount&& fee) override
        {
            try
            {
                WalletAddress senderAddress = wallet::createAddress(_walletDB);

                saveAddress(senderAddress, true); // should update the wallet_network

                ByteBuffer message(comment.begin(), comment.end());

                assert(!_wallet.expired());
                auto s = _wallet.lock();
                if (s)
                {
                    s->transfer_money(senderAddress.m_walletID, receiver, move(amount), move(fee), true, 120, move(message));
                }
            }
            catch (...)
            {

            }
        }

        void syncWithNode() override 
        {
            assert(!_nodeNetwork.expired());
            auto s = _nodeNetwork.lock();
            if (s)
                s->Connect();
        }

        void calcChange(beam::Amount&& amount) override 
        {
            auto coins = _walletDB->selectCoins(amount, false);
            Amount sum = 0;
            for (auto& c : coins)
            {
                sum += c.m_ID.m_Value;
            }
            if (sum < amount)
            {
                onChangeCalculated(0);
            }
            else
            {
                onChangeCalculated(sum - amount);
            }
        }

        void getWalletStatus() override 
        {
            LOG_DEBUG() << "getWalletStatus()";
            onStatus(getStatus());

            onTxStatus(beam::ChangeAction::Reset, _walletDB->getTxHistory());
            onAdrresses(false, _walletDB->getAddresses(false));
        }

        void getUtxosStatus() override 
        {
            LOG_DEBUG() << "getUtxosStatus()";
            onAllUtxoChanged(getUtxos());
        }

        void getAddresses(bool own) override 
        {
            LOG_DEBUG() << "getAddresses()";

            onAdrresses(own, _walletDB->getAddresses(own));
        }

        void cancelTx(const beam::TxID& id) override {}
        void deleteTx(const beam::TxID& id) override {}

        void generateNewAddress() override 
        {
            try
            {
                WalletAddress address = wallet::createAddress(_walletDB);

                onGeneratedNewAddress(address);
            }
            catch (...)
            {

            }
        }

        void changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID) override {}
        void deleteAddress(const beam::WalletID& id) override {}
        void saveAddressChanges(const beam::WalletID& id, const std::string& name, bool isNever, bool makeActive, bool makeExpired) override {}
        void setNodeAddress(const std::string& addr) override {}
        void changeWalletPassword(const beam::SecString& password) override {}
        void getNetworkStatus() override {}
        void refresh() override {}

        ///////////////////////////////////////////////
        // callbacks
        ///////////////////////////////////////////////

        void onStatus(const WalletStatus& status)
        {

            JNIEnv* env = Android_JNI_getEnv();

            jobject walletStatus = env->AllocObject(WalletStatusClass);

            setLongField(env, WalletStatusClass, walletStatus, "available", status.available);
            setLongField(env, WalletStatusClass, walletStatus, "unconfirmed", status.unconfirmed);

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

        void onTxStatus(beam::ChangeAction action, const std::vector<beam::TxDescription>& items) 
        {
            LOG_DEBUG() << "onTxStatus()";

            JNIEnv* env = Android_JNI_getEnv();

            jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onTxStatus", "(I[L" BEAM_JAVA_PATH "/entities/dto/TxDescriptionDTO;)V");
            
            jobjectArray txItems = 0;

            if(!items.empty())
            {
                txItems = env->NewObjectArray(static_cast<jsize>(items.size()), TxDescriptionClass, NULL);

                for(int i = 0; i < items.size(); ++i)
                {
                    const auto& item = items[i];

                    jobject tx = env->AllocObject(TxDescriptionClass);

                    setByteArrayField(env,         TxDescriptionClass, tx, "id", item.m_txId);
                    setLongField(env,             TxDescriptionClass, tx, "amount", item.m_amount);
                    setLongField(env,             TxDescriptionClass, tx, "fee", item.m_fee);
                    setLongField(env,             TxDescriptionClass, tx, "change", item.m_change);
                    setLongField(env,             TxDescriptionClass, tx, "minHeight", item.m_minHeight);

                    setStringField(env,         TxDescriptionClass, tx, "peerId", to_string(item.m_peerId));
                    setStringField(env,         TxDescriptionClass, tx, "myId", to_string(item.m_myId));

                    setByteArrayField(env,         TxDescriptionClass, tx, "message", item.m_message);
                    setLongField(env,             TxDescriptionClass, tx, "createTime", item.m_createTime);
                    setLongField(env,             TxDescriptionClass, tx, "modifyTime", item.m_modifyTime);
                    setBooleanField(env,         TxDescriptionClass, tx, "sender", item.m_sender);
                    setIntField(env,             TxDescriptionClass, tx, "status", static_cast<jint>(item.m_status));

                    env->SetObjectArrayElement(txItems, i, tx);
                }                
            }

            env->CallStaticVoidMethod(WalletListenerClass, callback, action, txItems);
        }

        void onSyncProgressUpdated(int done, int total)
        {
            LOG_DEBUG() << "onSyncProgressUpdated(" << done << ", " << total << ")";

            JNIEnv* env = Android_JNI_getEnv();

            jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onSyncProgressUpdated", "(II)V");

            env->CallStaticVoidMethod(WalletListenerClass, callback, done, total);
        }

        void onChangeCalculated(beam::Amount change) 
        {
            LOG_DEBUG() << "onChangeCalculated(" << change << ")";

            JNIEnv* env = Android_JNI_getEnv();

            jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onChangeCalculated", "(J)V");

            env->CallStaticVoidMethod(WalletListenerClass, callback, change);
        }

        void onAllUtxoChanged(const std::vector<beam::Coin>& utxosVec) 
        {
            LOG_DEBUG() << "onAllUtxoChanged()";

            JNIEnv* env = Android_JNI_getEnv();

            jobjectArray utxos = 0;

            if(!utxosVec.empty())
            {
                utxos = env->NewObjectArray(static_cast<jsize>(utxosVec.size()), UtxoClass, NULL);

                for(int i = 0; i < utxosVec.size(); ++i)
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

                    if(coin.m_createTxId)
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

        void onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses) 
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

        ///////////////////////////////////////////////
        // IWalletObserver impl
        ///////////////////////////////////////////////
        void onCoinsChanged() override
        {
            onAllUtxoChanged(getUtxos());
            onStatus(getStatus());
        }

        void onTransactionChanged(beam::ChangeAction action, vector<beam::TxDescription>&& items) override
        {
            onTxStatus(action, move(items));
            onStatus(getStatus());
        }

        void onSystemStateChanged() override
        {
            onStatus(getStatus());
        }

        void onAddressChanged() override
        {
            onAdrresses(true, _walletDB->getAddresses(true));
            onAdrresses(false, _walletDB->getAddresses(false));
        }

        void onSyncProgress(int done, int total) override
        {
            onSyncProgressUpdated(done, total);
        }

        WalletStatus getStatus() const
        {
            WalletStatus status{ _walletDB->getAvailable(), 0, 0, 0 };

            status.sent = wallet::getSpentByTx(_walletDB, TxStatus::Completed);
            status.received = wallet::getReceivedByTx(_walletDB, TxStatus::Completed);
            status.unconfirmed = _walletDB->getTotal(Coin::Incoming);
            status.update.lastTime = _walletDB->getLastUpdateTime();

            ZeroObject(status.stateID);
            _walletDB->getSystemStateID(status.stateID);

            return status;
        }

        vector<Coin> getUtxos() const
        {
            vector<Coin> utxos;
            _walletDB->visit([&utxos](const Coin& c)->bool
            {
                utxos.push_back(c);
                return true;
            });
            return utxos;
        }

        IWalletModelAsync::Ptr async;
        
    private:

        shared_ptr<thread> _thread;

        beam::IWalletDB::Ptr _walletDB;
        beam::io::Reactor::Ptr _reactor;
        IWalletModelAsync::Ptr _async;
        std::weak_ptr<beam::proto::FlyClient::INetwork> _nodeNetwork;
        std::weak_ptr<beam::IWalletNetwork> _walletNetwork;
        std::weak_ptr<beam::Wallet> _wallet;
        beam::io::Timer::Ptr _logRotateTimer;

        std::string _nodeAddrStr;

        shared_ptr<mutex> _startMutex;
        shared_ptr<condition_variable> _startCV;
    };

    vector<WalletModel> wallets;

    jobject regWallet(JNIEnv *env, jobject thiz)
    {
        wallets.push_back({});

        jobject walletObj = env->AllocObject(WalletClass);

        setLongField(env, WalletClass, walletObj, "_this", wallets.size() - 1);

        return walletObj;
    }

    WalletModel& getWallet(JNIEnv *env, jobject thiz)
    {
        jfieldID _this = env->GetFieldID(WalletClass, "_this", "J");
        return wallets[env->GetLongField(thiz, _this)];
    }

    void initLogger(const string& appData)
    {
        static auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, "wallet_", (fs::path(appData) / fs::path("logs")).string());

        Rules::get().UpdateChecksum();
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

    auto wallet = WalletDB::init(
        appData + "/" WALLET_FILENAME,
        pass,
        seed.hash());

    if(wallet)
    {
        LOG_DEBUG() << "wallet successfully created.";

        jobject walletObj = regWallet(env, thiz);

        getWallet(env, walletObj).start(JString(env, nodeAddrStr).value(), wallet);

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

JNIEXPORT jobject JNICALL BEAM_JAVA_API_INTERFACE(openWallet)(JNIEnv *env, jobject thiz, 
    jstring nodeAddrStr, jstring appDataStr, jstring passStr)
{
    auto appData = JString(env, appDataStr).value();

    initLogger(appData);

    LOG_DEBUG() << "opening wallet...";

    string pass = JString(env, passStr).value();
    auto wallet = WalletDB::open(appData + "/" WALLET_FILENAME, pass);

    if(wallet)
    {
        LOG_DEBUG() << "wallet successfully opened.";

        jobject walletObj = regWallet(env, thiz);

        getWallet(env, walletObj).start(JString(env, nodeAddrStr).value(), wallet);

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
    }

    return phrasesArray;
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(getWalletStatus)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "getWalletStatus()";

    getWallet(env, thiz).async->getWalletStatus();
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(getUtxosStatus)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "getUtxosStatus()";

    getWallet(env, thiz).async->getUtxosStatus();
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(syncWithNode)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "syncWithNode()";

    getWallet(env, thiz).async->syncWithNode();
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(sendMoney)(JNIEnv *env, jobject thiz,
    jstring receiverAddr, jstring comment, jlong amount, jlong fee)
{
    LOG_DEBUG() << "sendMoney(" << JString(env, receiverAddr).value() << ", " << JString(env, comment).value() << ", " << amount << ", " << fee << ")";

    WalletAddress peerAddr;
    peerAddr.m_walletID.FromHex(JString(env, receiverAddr).value());
    peerAddr.m_createTime = getTimestamp();

    // TODO: implement UI for this situation
    // TODO: don't save if you send to yourself
    getWallet(env, thiz).async->saveAddress(peerAddr, false);

    // TODO: show 'operation in process' animation here?
    getWallet(env, thiz).async->sendMoney(peerAddr.m_walletID
        , JString(env, comment).value()
        , beam::Amount(amount)
        , beam::Amount(fee));
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(calcChange)(JNIEnv *env, jobject thiz,
    jlong amount)
{
    LOG_DEBUG() << "calcChange(" << amount << ")";

    getWallet(env, thiz).async->calcChange(beam::Amount(amount));
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(getAddresses)(JNIEnv *env, jobject thiz,
    jboolean own)
{
    LOG_DEBUG() << "getAddresses(" << own << ")";

    getWallet(env, thiz).async->getAddresses(own);
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(generateNewAddress)(JNIEnv *env, jobject thiz)
{
    LOG_DEBUG() << "generateNewAddress()";

    getWallet(env, thiz).async->generateNewAddress();
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

    getWallet(env, thiz).async->saveAddress(addr, own);
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
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/Wallet");
        WalletClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/WalletStatusDTO");
        WalletStatusClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/SystemStateDTO");
        SystemStateClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/TxDescriptionDTO");
        TxDescriptionClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/UtxoDTO");
        UtxoClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
    }

    {
        jclass cls = env->FindClass(BEAM_JAVA_PATH "/entities/dto/WalletAddressDTO");
        WalletAddressClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
    }

    return JNI_VERSION_1_6;
}

#ifdef __cplusplus
}
#endif