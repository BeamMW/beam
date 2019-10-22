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

#include <emscripten/bind.h>

using namespace emscripten;
using namespace beam;
using namespace beam::wallet;

struct KeyIDV : Key::IDV
{   
    // !TODO: unfortunately, wasm doesn't support uint64_t as values 
    // max Number value in JS is 2^53-1, as variant, we could set hi/low bits for 64 bit values
    // or we could pass values as String and parse then
    // proof: https://emscripten.org/docs/getting_started/FAQ.html#how-do-i-pass-int64-t-and-uint64-t-values-from-js-into-wasm-functions

    uint32_t    getValue() const {return m_Value;}
    void        setValue(uint32_t value) {m_Value = value;}

    uint32_t    getIdx() const {return m_Idx;}
    void        setIdx(uint32_t value) {m_Idx = value;}

    uint32_t    getType() const {return m_Type;}
    void        setType(uint32_t value) {m_Type = value;}

    uint32_t    getSubIdx() const {return m_SubIdx;}
    void        setSubIdx(uint32_t value) {m_SubIdx = value;}    
};

struct KeyKeeper
{
    KeyKeeper(const beam::WordList& words)
    {

        ECC::NoLeak<ECC::uintBig> seed;
        auto buf = beam::decodeMnemonic(words);
        ECC::Hash::Processor() << beam::Blob(buf.data(), (uint32_t)buf.size()) >> seed.V;
        ECC::HKdf::Create(_kdf, seed.V);

        std::cout << "KeyKeeper(): " << seed.V << std::endl;

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

    ECC::Point GeneratePublicKey(const KeyIDV& id, bool createCoinKey) const
    {
        auto res = _impl->GeneratePublicKeySync(id, createCoinKey);

        std::cout << "GeneratePublicKey(): " << res << std::endl;

        return res;
    }

    std::string GetOwnerKey() const
    {
        const ECC::HKdf& kdf = static_cast<ECC::HKdf&>(*_kdf);

        auto publicKdf = std::make_shared<ECC::HKdfPub>();
        publicKdf->GenerateFrom(kdf);

        return "!TODO: convert to base16";
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

    class_<ECC::Point>("ECCPoint")
        .constructor()
    ;

    class_<KeyKeeper>("KeyKeeper")
        .constructor<const beam::WordList&>()
        .function("generatePublicKey", &KeyKeeper::GeneratePublicKey)
        .property("ownerKey", &KeyKeeper::GetOwnerKey)
        // .function("func", &KeyKeeper::func)
        // .property("prop", &KeyKeeper::getProp, &KeyKeeper::setProp)
        // .class_function("StaticFunc", &KeyKeeper::StaticFunc)
        ;
}
