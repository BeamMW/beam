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

int main()
{
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

    keyKeeper->get_SbbsKdf();

    return 0;
}
