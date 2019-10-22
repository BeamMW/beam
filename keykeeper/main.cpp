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
#include "core/block_rw.h"

#include <emscripten/bind.h>

using namespace emscripten;
using namespace beam;
using namespace beam::wallet;

namespace wasm
{
    char* to_hex(char* dst, const void* bytes, size_t size) 
    {
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

    std::vector<uint8_t> from_hex(const std::string& str, bool* wholeStringIsNumber)
    {
        size_t bias = (str.size() % 2) == 0 ? 0 : 1;
        assert((str.size() + bias) % 2 == 0);
        std::vector<uint8_t> res((str.size() + bias) >> 1);

        if (wholeStringIsNumber) *wholeStringIsNumber = true;

        for (size_t i = 0; i < str.size(); ++i)
        {
            auto c = str[i];
            size_t j = (i + bias) >> 1;
            res[j] <<= 4;
            if (c >= '0' && c <= '9')
            {
                res[j] += (c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                res[j] += 10 + (c - 'a');
            }
            else if (c >= 'A' && c <= 'F')
            {
                res[j] += 10 + (c - 'A');
            }
            else {
                if (wholeStringIsNumber) *wholeStringIsNumber = false;
                break;
            }
        }

        return res;
    }
};

struct KeyIDV
{   
    // !TODO: unfortunately, wasm doesn't support uint64_t as values 
    // we could pass values as String and parse then
    // proof: https://emscripten.org/docs/getting_started/FAQ.html#how-do-i-pass-int64-t-and-uint64-t-values-from-js-into-wasm-functions

    std::string getValue() const {return std::to_string(impl.m_Value);}
    void        setValue(const std::string& value) {impl.m_Value = std::stoull(value);}

    std::string getIdx() const {return std::to_string(impl.m_Idx);}
    void        setIdx(const std::string& value) {impl.m_Idx = std::stoull(value);}

    std::string getType() const {return std::to_string(impl.m_Type.V);}
    void        setType(const std::string& value) {impl.m_Type.V = std::stoull(value);}

    std::string getSubIdx() const {return std::to_string(impl.m_SubIdx);}
    void        setSubIdx(const std::string& value) {impl.m_SubIdx = std::stoull(value);}

    Key::IDV impl;
};

struct ECCPoint
{
    std::string getX() const 
    {
        return wasm::to_hex(impl.m_X.m_pData, ECC::uintBig::nBytes);
    }

    std::string getY() const 
    {
        return wasm::to_hex(&impl.m_Y, sizeof impl.m_Y);
    }

    ECC::Point impl;
};

struct KeyKeeper
{
    KeyKeeper(const beam::WordList& words)
    {
        static_assert(sizeof(uint64_t) == sizeof(unsigned long long));

        ECC::NoLeak<ECC::uintBig> seed;
        auto buf = beam::decodeMnemonic(words);
        ECC::Hash::Processor() << beam::Blob(buf.data(), (uint32_t)buf.size()) >> seed.V;
        ECC::HKdf::Create(_kdf, seed.V);

        struct VariablesDB : IVariablesDB
        {
            void setVarRaw(const char* name, const void* data, size_t size) override {};
            bool getVarRaw(const char* name, void* data, int size) const override { return false; };
            void removeVarRaw(const char* name) override {};
            void setPrivateVarRaw(const char* name, const void* data, size_t size) override {};
            bool getPrivateVarRaw(const char* name, void* data, int size) const override { return false; };
            bool getBlob(const char* name, ByteBuffer& var) const override { return false; };
        };

        IVariablesDB::Ptr vars = std::make_shared<VariablesDB>();

        _impl = std::make_shared<LocalPrivateKeyKeeper>(vars, _kdf);
    }

    ECCPoint GeneratePublicKey(const KeyIDV& id, bool createCoinKey) const
    {
        return ECCPoint{_impl->GeneratePublicKeySync(id.impl, createCoinKey)};
    }

    std::string GetOwnerKey(const std::string& pass) const
    {
        const ECC::HKdf& kdf = static_cast<ECC::HKdf&>(*_kdf);

        beam::KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
        ks.m_sMeta = std::to_string(0);

        ECC::HKdfPub pkdf;
        pkdf.GenerateFrom(kdf);

        ks.Export(pkdf);

        return ks.m_sRes;
    }

    size_t AllocateNonceSlot()
    {
        return _impl->AllocateNonceSlot();
    }

    ECCPoint GenerateNonce(size_t slot)
    {
        return ECCPoint{_impl->GenerateNonceSync(slot)};
    }

    void GenerateOutput(std::string schemeHeigh, const KeyIDV& id)
    {
        auto outputs = _impl->GenerateOutputsSync(std::stoull(schemeHeigh), {id.impl});

        for(auto& output : outputs)
        {
            
        }
    }

private:
    Key::IKdf::Ptr _kdf;
    IPrivateKeyKeeper::Ptr _impl;
};

// Binding code
EMSCRIPTEN_BINDINGS() 
{
    register_vector<std::string>("WordList");

    class_<KeyIDV>("KeyIDV")
        .constructor()
        .property("value",  &KeyIDV::getValue,  &KeyIDV::setValue)
        .property("idx",    &KeyIDV::getIdx,    &KeyIDV::setIdx)
        .property("type",   &KeyIDV::getType,   &KeyIDV::setType)
        .property("subIdx", &KeyIDV::getSubIdx, &KeyIDV::setSubIdx)
    ;
  
    class_<ECCPoint>("ECCPoint")
        .constructor()
        .property("x",  &ECCPoint::getX)
        .property("y",  &ECCPoint::getY)
    ;

    value_object<PersonRecord>("Output")
        .field("name", &PersonRecord::name)
        .field("age", &PersonRecord::age)
        ;

    class_<KeyKeeper>("KeyKeeper")
        .constructor<const beam::WordList&>()
        .function("generatePublicKey",  &KeyKeeper::GeneratePublicKey)
        .function("getOwnerKey",        &KeyKeeper::GetOwnerKey)
        .function("allocateNonceSlot",  &KeyKeeper::AllocateNonceSlot)
        .function("generateNonce",      &KeyKeeper::GenerateNonce)
        .function("generateOutput",     &KeyKeeper::GenerateOutput)
        // .function("func", &KeyKeeper::func)
        // .property("prop", &KeyKeeper::getProp, &KeyKeeper::setProp)
        // .class_function("StaticFunc", &KeyKeeper::StaticFunc)
        ;
}
