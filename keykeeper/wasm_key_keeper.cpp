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
#include "wallet/core/wallet_db.h"

#include <boost/algorithm/string.hpp>
#include "utility/string_helpers.cpp"
#include "nlohmann/json.hpp"

#include <emscripten/bind.h>

using namespace emscripten;
using namespace beam;
using namespace beam::wallet;
using json = nlohmann::json;
// #define PRINT_TEST_DATA 1

namespace beam
{
    char* to_hex(char* dst, const void* bytes, size_t size) {
        static const char digits[] = "0123456789abcdef";
        char* d = dst;

        const uint8_t* ptr = (const uint8_t*)bytes;
        const uint8_t* end = ptr + size;
        while (ptr < end) {
            uint8_t c = *ptr++;
            *d++ = digits[c >> 4];
            *d++ = digits[c & 0xF];
        }
        *d = '\0';
        return dst;
    }

    std::string to_hex(const void* bytes, size_t size) {
        char* buf = (char*)alloca(2 * size + 1);
        return std::string(to_hex(buf, bytes, size));
    }
}

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

    std::string GetWalletID()
    {
        return _impl2.GetWalletID();
    }

    std::string GetSbbsAddress(const std::string& ownID)
    {
        return _impl2.GetSbbsAddress(from_base64<uint64_t>(ownID));
    }

    std::string GetSbbsAddressPrivate(const std::string& ownID)
    {
        return _impl2.GetSbbsAddressPrivate(from_base64<uint64_t>(ownID));
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

        std::string GetWalletID() const
        {
            Key::ID kid(Zero);
            kid.m_Type = ECC::Key::Type::WalletID;

            ECC::Scalar::Native sk;
            m_pKdf->DeriveKey(sk, kid);
            PeerID pid;
            pid.FromSk(sk);
            return pid.str();
        }

        std::string GetSbbsAddress(uint64_t ownID)
        {
            WalletID walletID;
            get_SbbsWalletID(walletID, ownID);
            return std::to_string(walletID);
        }

        std::string GetSbbsAddressPrivate(uint64_t ownID)
        {
            ECC::Scalar::Native sk;
            WalletID walletID;
            get_SbbsWalletID(sk, walletID, ownID);
            return ECC::Scalar(sk).m_Value.str();
        }

        Key::IKdf::Ptr CreateSbbsKdf() 
        {
            IPrivateKeyKeeper2::Method::get_Kdf m;
            // trustless mode. create SBBS Kdf from a child PKdf. It won't be directly accessible from the owner key
            m.m_Root = false;
            m.m_iChild = Key::Index(-1); // definitely won't collude with a coin child Kdf (for coins high byte is reserved for scheme)

            InvokeSync(m);

            ECC::Scalar::Native sk;
            m.m_pPKdf->DerivePKey(sk, Zero);

            ECC::NoLeak<ECC::Scalar> s;
            s.V = sk;

            Key::IKdf::Ptr kdf;
            ECC::HKdf::Create(kdf, s.V.m_Value);
            return kdf;
        }

        // copied from wallet_db.cpp 
        // TODO move to common place
        Key::IKdf::Ptr get_SbbsKdf()// const
        {
            if (!m_pKdfSbbs)
            {
                m_pKdfSbbs = CreateSbbsKdf();
            }
            return m_pKdfSbbs;
        }

        void get_SbbsPeerID(ECC::Scalar::Native& sk, PeerID& pid, uint64_t ownID)
        {
            Key::IKdf::Ptr pKdfSbbs = get_SbbsKdf();
           // if (!pKdfSbbs)
           //     throw CannotGenerateSecretException();

            ECC::Hash::Value hv;
            Key::ID(ownID, Key::Type::Bbs).get_Hash(hv);

            pKdfSbbs->DeriveKey(sk, hv);
            pid.FromSk(sk);
        }

        void get_SbbsWalletID(ECC::Scalar::Native& sk, WalletID& wid, uint64_t ownID)
        {
            get_SbbsPeerID(sk, wid.m_Pk, ownID);

            // derive the channel from the address
            BbsChannel ch;
            wid.m_Pk.ExportWord<0>(ch);
            ch %= proto::Bbs::s_MaxWalletChannels;

            wid.m_Channel = ch;
        }

        void get_SbbsWalletID(WalletID& wid, uint64_t ownID)
        {
            ECC::Scalar::Native sk;
            get_SbbsWalletID(sk, wid, ownID);
        }

        //void get_Identity(PeerID& pid, uint64_t ownID)// const
        //{
        //    ECC::Hash::Value hv;
        //    Key::ID(ownID, Key::Type::WalletID).get_Hash(hv);
        //    ECC::Point::Native pt;
        //    get_OwnerKdf()->DerivePKeyG(pt, hv);
        //    pid = ECC::Point(pt).m_X;
        //}

        /*mutable*/ ECC::Key::IKdf::Ptr m_pKdfSbbs;
    };
    MyKeeKeeper _impl2;
};

// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<KeyKeeper>("KeyKeeper")
        .constructor<const std::string&>()
        .function("getOwnerKey",            &KeyKeeper::GetOwnerKey)
        .function("getWalletID",            &KeyKeeper::GetWalletID)
        .function("getSbbsAddress",         &KeyKeeper::GetSbbsAddress)
        .function("getSbbsAddressPrivate",  &KeyKeeper::GetSbbsAddressPrivate)
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
