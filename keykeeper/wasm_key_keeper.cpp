// Copyright 2019 The Beam Team
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

#include "local_private_key_keeper.h"
#include "mnemonic/mnemonic.h"
#include "wasm_key_keeper.h"

#include <boost/algorithm/string.hpp>
#include "utility/string_helpers.cpp"
#include "nlohmann/json.hpp"

#include <emscripten/bind.h>

using namespace emscripten;
using namespace beam;
using namespace beam::wallet;
using json = nlohmann::json;
// #define PRINT_TEST_DATA 1

struct KeyKeeper
{
    KeyKeeper(const std::string& phrase)
        : _impl2(CreateKdfFromSeed(phrase))
    {
        static_assert(sizeof(uint64_t) == sizeof(unsigned long long));
    }

    std::string GetOwnerKey(const std::string& pass)
    {
        IPrivateKeyKeeper2::Method::get_Kdf method;
        method.m_Root = true;
        method.m_iChild = 0;
        _impl2.InvokeSync(method);

        beam::KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(0);

        ks.ExportP(*method.m_pPKdf);

        return ks.m_sRes;
    }

    std::string get_Kdf(bool root, Key::Index keyIndex)
    {
        IPrivateKeyKeeper2::Method::get_Kdf method;
        method.m_Root = root;
        method.m_iChild = keyIndex;
        auto status = _impl2.InvokeSync(method);
        json res =
        {
            {JsonFields::Status, status}
        };

        if (status == IPrivateKeyKeeper2::Status::Success)
        {
            ByteBuffer buf(sizeof(ECC::HKdfPub::Packed), 0);
            method.m_pPKdf->ExportP(&buf[0]);
            
            res.push_back({ JsonFields::PublicKdf, to_base64(buf) });
        }
        return res.dump();
    }

    std::string get_NumSlots()
    {
        IPrivateKeyKeeper2::Method::get_NumSlots method;
        auto status = _impl2.InvokeSync(method);
        json res =
        {
            {JsonFields::Status, status}
        };

        if (status == IPrivateKeyKeeper2::Status::Success)
        {
            res.push_back({ JsonFields::Count, method.m_Count });
        }
        return res.dump();
    }

    std::string CreateOutput(const std::string& scheme, const std::string& cid)
    {
        IPrivateKeyKeeper2::Method::CreateOutput method;

        method.m_hScheme = from_base64<Height>(scheme);
        method.m_Cid = from_base64<CoinID>(cid);

        auto status = _impl2.InvokeSync(method);
        json res =
        {
            {JsonFields::Status, status}
        };

        if (status == IPrivateKeyKeeper2::Status::Success)
        {
            res.push_back({ JsonFields::Result, to_base64(method.m_pResult) });
        }
        return res.dump();
    }
    
    std::string SignReceiver(const std::string& inputs
                           , const std::string& outputs
                           , const std::string& kernel
                           , bool nonConventional
                           , const std::string& peerID
                           , const std::string& myIDKey)
    {
        IPrivateKeyKeeper2::Method::SignReceiver method;

        method.m_vInputs = from_base64<std::vector<CoinID>>(inputs);
        method.m_vOutputs = from_base64<std::vector<CoinID>>(outputs);
        method.m_pKernel = from_base64<TxKernelStd::Ptr>(kernel);
        method.m_NonConventional = nonConventional;
        method.m_Peer = from_base64<PeerID>(peerID);
        method.m_MyIDKey = from_base64<WalletIDKey>(myIDKey);

        auto status = _impl2.InvokeSync(method);
        json res =
        {
            {JsonFields::Status, status}
        };

        if (status == IPrivateKeyKeeper2::Status::Success)
        {
            res.push_back({ JsonFields::PaymentProofSig, to_base64(method.m_PaymentProofSignature) });
            FillCommonSignatureResult(res, method);
        }
        return res.dump();
    }

    std::string SignSender(const std::string& inputs
                         , const std::string& outputs
                         , const std::string& kernel
                         , bool nonConventional
                         , const std::string& peerID
                         , const std::string& myIDKey
                         , uint32_t slot
                         , const std::string& userAgreement
                         , const std::string& myID
                         , const std::string& paymentProof)
    {
        IPrivateKeyKeeper2::Method::SignSender method;

        method.m_vInputs = from_base64<std::vector<CoinID>>(inputs);
        method.m_vOutputs = from_base64<std::vector<CoinID>>(outputs);
        method.m_pKernel = from_base64<TxKernelStd::Ptr>(kernel);
        method.m_NonConventional = nonConventional;
        method.m_Peer = from_base64<PeerID>(peerID);
        method.m_MyIDKey = from_base64<WalletIDKey>(myIDKey);
        method.m_Slot = slot;
        method.m_UserAgreement = from_base64<ECC::Hash::Value>(userAgreement);
        method.m_MyID = from_base64<PeerID>(myID);
        method.m_PaymentProofSignature = from_base64<ECC::Signature>(paymentProof);

        bool zeroAgreement = method.m_UserAgreement == Zero;

        auto status = _impl2.InvokeSync(method);
        json res =
        {
            {JsonFields::Status, status}
        };

        if (status == IPrivateKeyKeeper2::Status::Success)
        {
            if (zeroAgreement)
            {
                res.push_back({ JsonFields::UserAgreement, to_base64(method.m_UserAgreement) });
                res.push_back({ JsonFields::Commitment, to_base64(method.m_pKernel->m_Commitment) });
                res.push_back({ JsonFields::PublicNonce, to_base64(method.m_pKernel->m_Signature.m_NoncePub) });
            }
            else
            {
                FillCommonSignatureResult(res, method);
            }
        }
        return res.dump();
    }

    std::string SignSplit(const std::string& inputs
                        , const std::string& outputs
                        , const std::string& kernel
                        , bool nonConventional)
    {
        IPrivateKeyKeeper2::Method::SignSplit method;

        method.m_vInputs = from_base64<std::vector<CoinID>>(inputs);
        method.m_vOutputs = from_base64<std::vector<CoinID>>(outputs);
        method.m_pKernel = from_base64<TxKernelStd::Ptr>(kernel);
        method.m_NonConventional = nonConventional;

        auto status = _impl2.InvokeSync(method);
        json res =
        {
            {JsonFields::Status, status}
        };

        if (status == IPrivateKeyKeeper2::Status::Success)
        {
            FillCommonSignatureResult(res, method);
        }
        return res.dump();
    }

    void FillCommonSignatureResult(json& res, const IPrivateKeyKeeper2::Method::TxCommon& method)
    {
        res.push_back({ JsonFields::Offset, to_base64(ECC::Scalar(method.m_kOffset)) });
        res.push_back({ JsonFields::Kernel, to_base64(method.m_pKernel) });
    }

    static std::string GeneratePhrase()
    {
        return boost::join(createMnemonic(getEntropy(), language::en), " ");
    }

    static bool IsAllowedWord(const std::string& word)
    {
        return isAllowedWord(word, language::en);
    }

    static bool IsValidPhrase(const std::string& words)
    {
        return isValidMnemonic(string_helpers::split(words, ' '), language::en);
    }

    // TODO: move to common place
    static ECC::Key::IKdf::Ptr CreateKdfFromSeed(const std::string& phrase)
    {
        if (!IsValidPhrase(phrase))
            throw "Invalid seed phrase";

        Key::IKdf::Ptr kdf;
        ECC::NoLeak<ECC::uintBig> seed;
        auto buf = beam::decodeMnemonic(string_helpers::split(phrase, ' '));
        ECC::Hash::Processor() << beam::Blob(buf.data(), (uint32_t)buf.size()) >> seed.V;
        ECC::HKdf::Create(kdf, seed.V);
        return kdf;
    }

private:
    struct MyKeeKeeper
        : public LocalPrivateKeyKeeperStd
    {
        using LocalPrivateKeyKeeperStd::LocalPrivateKeyKeeperStd;

        bool IsTrustless() override { return true; }
    };
    MyKeeKeeper _impl2;
};

// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<KeyKeeper>("KeyKeeper")
        .constructor<const std::string&>()
        .function("getOwnerKey",            &KeyKeeper::GetOwnerKey)
#define THE_MACRO(method) \
        .function(#method, &KeyKeeper::method)\

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

        .class_function("GeneratePhrase",   &KeyKeeper::GeneratePhrase)
        .class_function("IsAllowedWord",    &KeyKeeper::IsAllowedWord)
        .class_function("IsValidPhrase",    &KeyKeeper::IsValidPhrase)
        // .function("func", &KeyKeeper::func)
        // .property("prop", &KeyKeeper::getProp, &KeyKeeper::setProp)
        // .class_function("StaticFunc", &KeyKeeper::StaticFunc)
        ;
}
