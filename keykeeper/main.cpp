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

struct KeyKeeper
{
    KeyKeeper(const beam::WordList& words)
    {
        ECC::NoLeak<ECC::uintBig> seed;
        auto buf = beam::decodeMnemonic(words);
        ECC::Hash::Processor() << beam::Blob(buf.data(), (uint32_t)buf.size()) >> seed.V;
        ECC::HKdf::Create(_kdf, seed.V);

        std::cout << seed.V << std::endl;
    }

private:
    beam::Key::IKdf::Ptr _kdf;
};

// Binding code
EMSCRIPTEN_BINDINGS() {

    register_vector<std::string>("WordList");
    class_<KeyKeeper>("KeyKeeper")
        .constructor<const beam::WordList&>()
        // .function("func", &KeyKeeper::func)
        // .property("prop", &KeyKeeper::getProp, &KeyKeeper::setProp)
        // .class_function("StaticFunc", &KeyKeeper::StaticFunc)
        ;
}

extern "C"
{

void runTest()
{
    std::cout << "Start Key Keeper Test..." << std::endl;

    using namespace beam;
    using namespace beam::wallet;

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

    Key::IKdf::Ptr kdf;
    {
        ECC::NoLeak<ECC::uintBig> seed;
        auto buf = beam::decodeMnemonic({ "copy", "vendor", "shallow", "raven", "coffee", "appear", "book", "blast", "lock", "exchange", "farm", "glue" });
        ECC::Hash::Processor() << Blob(buf.data(), (uint32_t)buf.size()) >> seed.V;
        ECC::HKdf::Create(kdf, seed.V);
    }

    IPrivateKeyKeeper::Ptr keyKeeper = std::make_shared<LocalPrivateKeyKeeper>(vars, kdf);

    {
        std::cout << std::endl << "Generating Nonce..." << std::endl;
        std::cout << "===================" << std::endl;

        auto slot = keyKeeper->AllocateNonceSlot();
        auto nonce = keyKeeper->GenerateNonceSync(slot);

        std::cout << "Nonce[" << slot << "] = " << nonce << std::endl;
    }

    {
        std::cout << std::endl << "Generating Public Key..." << std::endl;
        std::cout << "===================" << std::endl;

        const ECC::Key::IDV kidv(100500, 15, Key::Type::Regular, 7, ECC::Key::IDV::Scheme::V0);

        std::cout << kidv << std::endl;

        ECC::Point pt2 = keyKeeper->GeneratePublicKeySync(kidv, true);

        std::cout << "Public key: " << pt2 << std::endl;
    }
}

}
