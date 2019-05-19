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

#pragma once

#include <string>
#include <vector>
#include <jni.h>

#include <core/ecc.h>

#define CONCAT1(prefix, class, function)    CONCAT2(prefix, class, function)
#define CONCAT2(prefix, class, function)    Java_ ## prefix ## _ ## class ## _ ## function

#define DEF2STR2(x) #x
#define DEF2STR(x) DEF2STR2(x)

#define BEAM_JAVA_PACKAGE(sep)                     com ## sep ## mw ## sep ## beam ## sep ## beamwallet ## sep ## core
#define BEAM_JAVA_PREFIX                         BEAM_JAVA_PACKAGE(_)
#define BEAM_JAVA_PATH                             "com/mw/beam/beamwallet/core" // doesn't work on clang DEF2STR(BEAM_JAVA_PACKAGE(/))
#define BEAM_JAVA_API_INTERFACE(function)         CONCAT1(BEAM_JAVA_PREFIX, Api, function)
#define BEAM_JAVA_WALLET_INTERFACE(function)     CONCAT1(BEAM_JAVA_PREFIX, entities_Wallet, function)

extern JavaVM* JVM;

extern jclass WalletListenerClass;
extern jclass WalletClass;
extern jclass WalletStatusClass;
extern jclass SystemStateClass;
extern jclass TxDescriptionClass;
extern jclass UtxoClass;
extern jclass WalletAddressClass;
extern jclass PaymentInfoClass;

JNIEnv* Android_JNI_getEnv(void);

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
        env->DeleteLocalRef(name);
    }

    std::string value() const
    {
        return data;
    }
private:
    JNIEnv* env;
    jstring name;
    jboolean isCopy;
    const char* data;
};

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
    jstring str = env->NewStringUTF(value.c_str());
    env->SetObjectField(obj, fieldId, str);

    env->DeleteLocalRef(str);
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
        env->DeleteLocalRef(hash);
    }
}

template <>
inline void setByteArrayField<ECC::uintBig>(JNIEnv *env, jclass clazz, jobject obj, const char* name, const ECC::uintBig& value)
{
    std::vector<uint8_t> data;
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

        env->DeleteLocalRef(byteArray);
        return size;
    }

    return 0;
}