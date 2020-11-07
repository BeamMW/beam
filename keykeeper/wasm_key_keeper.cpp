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

    std::string GetIdentity(const std::string& keyIDstr)
    {
        return _impl2.GetIdentity(std::stoull(keyIDstr)).str();
    }

    std::string GetSbbsAddress(const std::string& ownIDstr)
    {
        return _impl2.GetSbbsAddress(std::stoull(ownIDstr));
    }

    std::string GetSbbsAddressPrivate(const std::string& ownIDstr)
    {
        return _impl2.GetSbbsAddressPrivate(std::stoull(ownIDstr));
    }

    std::string GetSendToken(const std::string& sbbsAddress, const std::string& identityStr, const std::string& amountStr)
    {
        auto amountStrCopy = boost::erase_all_copy(amountStr, "-");
        beam::Amount amount = amountStrCopy.empty() ? 0 : std::stoull(amountStrCopy);
        
        return wallet::GetSendToken(sbbsAddress, identityStr, amount);
    }

    std::string InvokeServiceMethod(const std::string& data)
    {
        json msg = json::parse(data);
        json params = msg["params"];
        std::string method = msg["method"];
        json res = 
        {
            {"jsonrpc", "2.0"},
            {"id", msg["id"]}
        };

        // !TODO: we have to check all the parameters for every method and throw an error if smth wrong

             if(method == "get_kdf")       res["result"] = get_Kdf(params["root"], params["child_key_num"]);
        else if(method == "get_slots")     res["result"] = get_NumSlots();
        else if(method == "create_output") res["result"] = CreateOutput(params["scheme"], params["id"]);
        else if(method == "sign_receiver") res["result"] = SignReceiver(params["inputs"], params["outputs"], params["kernel"], params["non_conv"], params["peer_id"], params["my_id_key"]);
        else if(method == "sign_sender")   res["result"] = SignSender(params["inputs"], params["outputs"], params["kernel"], params["non_conv"], params["peer_id"], params["my_id_key"], params["slot"], params["agreement"], params["my_id"], params["payment_proof_sig"]);
        else if(method == "sign_split")    res["result"] = SignSplit(params["inputs"], params["outputs"], params["kernel"], params["non_conv"]);
        else res["error"] = 
            {
                {"code", -32601}, 
                {"message", "Procedure not found."}, 
                {"data", ("unknown method: " + method)}
            };

        return res.dump();
    }

    json get_Kdf(bool root, Key::Index keyIndex)
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
        return res;
    }

    json get_NumSlots()
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
        return res;
    }

    json CreateOutput(const std::string& scheme, const std::string& cid)
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
        return res;
    }
    
    json SignReceiver(const std::string& inputs
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
        return res;
    }

    json SignSender(const std::string& inputs
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
        return res;
    }

    json SignSplit(const std::string& inputs
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
        return res;
    }

    void FillCommonSignatureResult(json& res, const IPrivateKeyKeeper2::Method::TxCommon& method)
    {
        res.push_back({ JsonFields::Offset, to_base64(ECC::Scalar(method.m_kOffset)) });
        res.push_back({ JsonFields::Kernel, to_base64(method.m_pKernel) });
    }

    static std::string GeneratePhrase()
    {
        return boost::join(createMnemonic(getEntropy()), " ");
    }

    static bool IsAllowedWord(const std::string& word)
    {
        return isAllowedWord(word);
    }

    static bool IsValidPhrase(const std::string& words)
    {
        return isValidMnemonic(string_helpers::split(words, ' '));
    }

    static std::string ConvertTokenToJson(const std::string& token)
    {
        return wallet::ConvertTokenToJson(token);
    }

    static std::string ConvertJsonToToken(const std::string& jsonParams)
    {
        return wallet::ConvertJsonToToken(jsonParams);
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

    struct MyKeyKeeper
        : public LocalPrivateKeyKeeperStd
    {
        static const Slot::Type s_DefNumSlots = 1024;

        MyKeyKeeper(const Key::IKdf::Ptr& pKdf)
            : LocalPrivateKeyKeeperStd(pKdf, s_DefNumSlots)
        {
            // WASM key keeper doesn't save and doesn't keep slot values
            ECC::GenRandom(m_State.m_hvLast);
        }

        virtual void Regenerate(Slot::Type iSlot) override
        {
            // instead of regenerating the slot - just delete it
            State::UsedMap::iterator it = m_State.m_Used.find(iSlot);
            if (m_State.m_Used.end() != it)
                m_State.m_Used.erase(it);
        }

        bool IsTrustless() override { return true; }

        std::string GetWalletID() const
        {
            return GetIdentity(0).str();
        }

        PeerID GetIdentity(uint64_t keyID) const
        {
            ECC::Scalar::Native sk;
            m_pKdf->DeriveKey(sk, Key::ID(keyID, ECC::Key::Type::WalletID));
            PeerID pid;
            pid.FromSk(sk);
            return pid;
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

        ECC::Key::IKdf::Ptr m_pKdfSbbs;
    };
    MyKeyKeeper _impl2;
};

// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<KeyKeeper>("KeyKeeper")
        .constructor<const std::string&>()
        .function("getOwnerKey",            &KeyKeeper::GetOwnerKey)
        .function("getWalletID",            &KeyKeeper::GetWalletID)
        .function("getIdentity",            &KeyKeeper::GetIdentity)
        .function("getSendToken",           &KeyKeeper::GetSendToken)
        .function("getSbbsAddress",         &KeyKeeper::GetSbbsAddress)
        .function("getSbbsAddressPrivate",  &KeyKeeper::GetSbbsAddressPrivate)
        .function("invokeServiceMethod",    &KeyKeeper::InvokeServiceMethod)
        .class_function("GeneratePhrase",   &KeyKeeper::GeneratePhrase)
        .class_function("IsAllowedWord",    &KeyKeeper::IsAllowedWord)
        .class_function("IsValidPhrase",    &KeyKeeper::IsValidPhrase)
        .class_function("ConvertTokenToJson",&KeyKeeper::ConvertTokenToJson)
        .class_function("ConvertJsonToToken", &KeyKeeper::ConvertJsonToToken)
        ;
}
