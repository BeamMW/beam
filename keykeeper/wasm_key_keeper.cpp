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
#include "3rdparty/utilstrencodings.h"

#include <emscripten/bind.h>

using namespace emscripten;
using namespace beam;
using namespace beam::wallet;

// #define PRINT_TEST_DATA 1

namespace
{
    template <typename T>
    std::string to_base64(const T& obj)
    {
        ByteBuffer buffer;
        {
            Serializer s;
            s & obj;
            s.swap_buf(buffer);
        }

        return EncodeBase64(buffer.data(), buffer.size());
    }

    template <>
    std::string to_base64(const KernelParameters& obj)
    {
        ByteBuffer buffer;
        {
            Serializer s;
            s   
                & obj.height.m_Min
                & obj.height.m_Max
                & obj.fee
                & obj.commitment;
            s.swap_buf(buffer);
        }

        return EncodeBase64(buffer.data(), buffer.size());
    }

    template <typename T>
    T&& from_base64(const std::string& base64)
    {
        T obj;
        {
            auto data = DecodeBase64(base64.data());

            Deserializer d;
            d.reset(data.data(), data.size());

            d & obj;
        }

        return std::move(obj);
    }

    template <>
    KernelParameters&& from_base64(const std::string& base64)
    {
        KernelParameters obj;
        {
            auto data = DecodeBase64(base64.data());

            Deserializer d;
            d.reset(data.data(), data.size());

            d   
                & obj.height.m_Min
                & obj.height.m_Max
                & obj.fee
                & obj.commitment;
        }

        return std::move(obj);
    }
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

    std::string GeneratePublicKey(const std::string& kidv, bool createCoinKey) const
    {
#if defined(PRINT_TEST_DATA)
        {
            std::cout 
                << "ECC::Key::IDV(100500, 15, Key::Type::Regular, 7, ECC::Key::IDV::Scheme::V0) -> data:application/octet-stream;base64,"
                << to_base64(ECC::Key::IDV(100500, 15, Key::Type::Regular, 7, ECC::Key::IDV::Scheme::V0))
                << std::endl;
        }
#endif

        return to_base64(_impl->GeneratePublicKeySync(from_base64<ECC::Key::IDV>(kidv), createCoinKey));
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

    std::string GenerateNonce(size_t slot)
    {
        return to_base64(_impl->GenerateNonceSync(slot));
    }

    std::string GenerateOutput(const std::string& schemeHeigh, const std::string& kidv)
    {
#if defined(PRINT_TEST_DATA)
        {
            Height schemeHeigh = 100500;
            std::cout 
                << "Height schemeHeigh -> data:application/octet-stream;base64,"
                << to_base64(schemeHeigh)
                << std::endl;
        }
#endif
        // TODO: switch to generate vector<> instead single output
        auto outputs = _impl->GenerateOutputsSync(from_base64<Height>(schemeHeigh), {from_base64<ECC::Key::IDV>(kidv)});

        return to_base64(outputs[0]);
    }

    std::string Sign(const std::string& inputs, const std::string& outputs, const std::string& offset, size_t nonceSlot, const std::string& kernelParameters, const std::string& publicNonce)
    {
#if defined(PRINT_TEST_DATA)
        {
            ECC::Point::Native totalPublicExcess = Zero;

            std::vector<ECC::Key::IDV> inputCoins =
            {
                {40, 0, Key::Type::Regular},
            };

            std::vector<ECC::Key::IDV> outputCoins =
            {
                {16, 0, Key::Type::Regular},
                {24, 0, Key::Type::Regular},
            };

            beam::Amount fee = 0;
            ECC::Scalar::Native offset = Zero;
            offset.GenRandomNnz();

            std::cout 
                << "std::vector<Key::IDV> inputs -> data:application/octet-stream;base64,"
                << to_base64(inputCoins)
                << std::endl;

            std::cout 
                << "std::vector<Key::IDV> outputs -> data:application/octet-stream;base64,"
                << to_base64(outputCoins)
                << std::endl;

            std::cout 
                << "ECC::Scalar offset -> data:application/octet-stream;base64,"
                << to_base64(ECC::Scalar(offset))
                << std::endl;

            {

                ECC::Point::Native publicAmount = Zero;
                Amount amount = 0;
                for (const auto& cid : inputCoins)
                {
                    amount += cid.m_Value;
                }
                AmountBig::AddTo(publicAmount, amount);
                amount = 0;
                publicAmount = -publicAmount;
                for (const auto& cid : outputCoins)
                {
                    amount += cid.m_Value;
                }
                AmountBig::AddTo(publicAmount, amount);

                ECC::Point::Native publicExcess = ECC::Context::get().G * offset;

                {
                    ECC::Point::Native commitment;

                    for (const auto& output : outputCoins)
                    {
                        if (commitment.Import(_impl->GeneratePublicKeySync(output, true)))
                        {
                            publicExcess += commitment;
                        }
                    }

                    publicExcess = -publicExcess;
                    for (const auto& input : inputCoins)
                    {
                        if (commitment.Import(_impl->GeneratePublicKeySync(input, true)))
                        {
                            publicExcess += commitment;
                        }
                    }
                }

                publicExcess += publicAmount;

                totalPublicExcess = publicExcess;
            }

            KernelParameters kernelParameters;
            kernelParameters.fee = fee;
            kernelParameters.height = { 25000, 27500 };
            kernelParameters.commitment = totalPublicExcess;

            std::cout 
                << "KernelParameters kernelParamerters -> data:application/octet-stream;base64,"
                << to_base64(kernelParameters)
                << std::endl;

            ECC::Point::Native peerPublicNonce = Zero;
            ECC::Point::Native publicNonce;
            uint8_t nonceSlot = (uint8_t)_impl->AllocateNonceSlot();
            publicNonce.Import(_impl->GenerateNonceSync(nonceSlot));

            ECC::Signature signature;
            signature.m_NoncePub = publicNonce + peerPublicNonce;

            std::cout 
                << "publicNonce -> data:application/octet-stream;base64,"
                << to_base64(signature.m_NoncePub)
                << std::endl;
        }
#endif

        ECC::Scalar::Native offsetNative;
        offsetNative.Import(from_base64<ECC::Scalar>(offset));

        ECC::Point::Native publicNonceNative;
        publicNonceNative.Import(from_base64<ECC::Point>(publicNonce));

        auto sign = _impl->SignSync(
            from_base64<std::vector<Key::IDV>>(inputs), 
            from_base64<std::vector<Key::IDV>>(outputs),
            offsetNative,
            nonceSlot,
            from_base64<KernelParameters>(kernelParameters),
            publicNonceNative);

        return to_base64(sign);
    }

private:
    Key::IKdf::Ptr _kdf;
    IPrivateKeyKeeper::Ptr _impl;
};

// Binding code
EMSCRIPTEN_BINDINGS() 
{
    register_vector<std::string>("WordList");

    class_<KeyKeeper>("KeyKeeper")
        .constructor<const beam::WordList&>()
        .function("generatePublicKey",  &KeyKeeper::GeneratePublicKey)
        .function("getOwnerKey",        &KeyKeeper::GetOwnerKey)
        .function("allocateNonceSlot",  &KeyKeeper::AllocateNonceSlot)
        .function("generateNonce",      &KeyKeeper::GenerateNonce)
        .function("generateOutput",     &KeyKeeper::GenerateOutput)
        .function("sign",               &KeyKeeper::Sign)
        // .function("func", &KeyKeeper::func)
        // .property("prop", &KeyKeeper::getProp, &KeyKeeper::setProp)
        // .class_function("StaticFunc", &KeyKeeper::StaticFunc)
        ;
}
