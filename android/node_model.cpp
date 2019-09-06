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
// limitations under the License

#include "node_model.h"
#include "node/node.h"
#include <mutex>

#include "pow/external_pow.h"

#include <boost/filesystem.hpp>
#include "utility/logger.h"
#include "wallet/default_peers.h"

#include <jni.h>
#include "common.h"

using namespace beam;
using namespace beam::io;
using namespace std;

NodeModel::NodeModel(const std::string& appPath)
    : m_nodeClient(this)
    , m_appPath(appPath)
{
}

void NodeModel::setKdf(beam::Key::IKdf::Ptr kdf)
{
    m_nodeClient.setKdf(kdf);
}

void NodeModel::startNode()
{
    m_nodeClient.startNode();
}

void NodeModel::stopNode()
{
    m_nodeClient.stopNode();
}

void NodeModel::start()
{
    m_nodeClient.start();
}

bool NodeModel::isNodeRunning() const
{
    return m_nodeClient.isNodeRunning();
}

void NodeModel::onInitProgressUpdated(uint64_t done, uint64_t total)
{
    LOG_DEBUG() << "onInitProgressUpdated(" << done << ", " << total << ")";
}

void NodeModel::onSyncProgressUpdated(int done, int total)
{
    LOG_DEBUG() << "onNodeSyncProgressUpdated(" << done << ", " << total << ")";

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeSyncProgressUpdated", "(II)V");

    env->CallStaticVoidMethod(WalletListenerClass, callback, done, total);
}

void NodeModel::onStartedNode()
{
    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onStartedNode", "()V");

    env->CallStaticVoidMethod(WalletListenerClass, callback);
}

void NodeModel::onStoppedNode()
{
    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onStoppedNode", "()V");

    env->CallStaticVoidMethod(WalletListenerClass, callback);
}

void NodeModel::onNodeCreated()
{
    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeCreated", "()V");

    env->CallStaticVoidMethod(WalletListenerClass, callback);
}

void NodeModel::onNodeDestroyed()
{
    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeDestroyed", "()V");

    env->CallStaticVoidMethod(WalletListenerClass, callback);
}

// void NodeModel::onFailedToStartNode()
// {
//     JNIEnv* env = Android_JNI_getEnv();

//     jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onFailedToStartNode", "()V");

//     env->CallStaticVoidMethod(WalletListenerClass, callback);
// }

void NodeModel::onSyncError(beam::Node::IObserver::Error error)
{
}

void NodeModel::onFailedToStartNode(io::ErrorCode /*errorCode*/)
{
    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onFailedToStartNode", "()V");

    env->CallStaticVoidMethod(WalletListenerClass, callback);
}

uint16_t NodeModel::getLocalNodePort()
{
    // default value
    return 10005;
}

std::string NodeModel::getLocalNodeStorage()
{
    return m_appPath + "/node.db";
}

std::string NodeModel::getTempDir()
{
    return m_appPath + "/temp";
}

std::vector<std::string> NodeModel::getLocalNodePeers()
{
    return getDefaultPeers();
}

void NodeModel::onNodeThreadFinished()
{
    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "onNodeThreadFinished", "()V");

    env->CallStaticVoidMethod(WalletListenerClass, callback);
}
