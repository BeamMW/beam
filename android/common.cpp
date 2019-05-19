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

#include "common.h"

JavaVM* JVM = nullptr;

jclass WalletListenerClass = 0;
jclass WalletClass = 0;
jclass WalletStatusClass = 0;
jclass SystemStateClass = 0;
jclass TxDescriptionClass = 0;
jclass UtxoClass = 0;
jclass WalletAddressClass = 0;
jclass PaymentInfoClass = 0;

namespace
{
    class ThreadJNIEnv
    {
    public:

        ThreadJNIEnv()
        {
#if defined (__ANDROID__)
            JVM->AttachCurrentThread(&m_env, NULL);
#else
            JVM->AttachCurrentThread((void**)&m_env, NULL);
#endif
        }

        ~ThreadJNIEnv()
        {
            JVM->DetachCurrentThread();
        }

        JNIEnv* get() const
        {
            return m_env;
        }

    private:
        JNIEnv* m_env;
    };
}

JNIEnv* Android_JNI_getEnv(void)
{
    static thread_local ThreadJNIEnv env;
    return env.get();
}