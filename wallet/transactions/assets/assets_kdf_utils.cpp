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
#include "assets_kdf_utils.h"
#include "proto.h"

namespace beam::wallet {
    namespace {
        std::pair<PeerID, ECC::Scalar::Native> GetAssetOwnerKeypair(const Key::IKdf::Ptr& masterKdf, Key::Index assetOwnerIdx)
        {
            ECC::Scalar::Native skAssetOwnerSk;
            masterKdf->DeriveKey(skAssetOwnerSk, beam::Key::ID(assetOwnerIdx, beam::Key::Type::Asset));

            beam::PeerID assetOwnerId;
            assetOwnerId.FromSk(skAssetOwnerSk);

            return std::make_pair(assetOwnerId, std::move(skAssetOwnerSk));
        }
    }

    PeerID GetAssetOwnerID(const Key::IKdf::Ptr& masterKdf, Key::Index assetOwnerIdx)
    {
        return GetAssetOwnerKeypair(masterKdf, assetOwnerIdx).first;
    }

    std::vector<Input::Ptr> GenerateAssetInputs(const Key::IKdf::Ptr& masterKdf, const CoinIDList& coins)
    {
        std::vector<beam::Input::Ptr> inputs;
        inputs.reserve(coins.size());
        for (const auto &cid : coins)
        {
            inputs.emplace_back();
            inputs.back().reset(new beam::Input);

            ECC::Scalar::Native sk;
            beam::CoinID::Worker(cid).Create(sk, inputs.back()->m_Commitment, *cid.get_ChildKdf(masterKdf));
        }
        return inputs;
    }

    std::vector<Output::Ptr> GenerateAssetOutputs(const Key::IKdf::Ptr& masterKdf, Height minHeight, const CoinIDList& coins)
    {
        std::vector<Output::Ptr> outputs;
        outputs.reserve(coins.size());
        for (const auto& cid : coins)
        {
            outputs.emplace_back();
            outputs.back().reset(new Output);

            ECC::Scalar::Native sk;
            outputs.back()->Create(minHeight, sk, *cid.get_ChildKdf(masterKdf), cid, *masterKdf);
        }
        return outputs;
    }

    ECC::Scalar::Native GetExcess(const Key::IKdf::Ptr& masterKdf, const CoinIDList& inputs, const CoinIDList& outputs)
    {
        // Excess = Sum(input blinfing factors) - Sum(output blinfing factors) - offset
        ECC::Point commitment;
        ECC::Scalar::Native blindingFactor;
        ECC::Scalar::Native excess = Zero;

        for (const auto& coinID : outputs)
        {
            CoinID::Worker(coinID).Create(blindingFactor, commitment, *coinID.get_ChildKdf(masterKdf));
            excess += blindingFactor;
        }

        excess = -excess;
        for (const auto& coinID : inputs)
        {
            CoinID::Worker(coinID).Create(blindingFactor, commitment, *coinID.get_ChildKdf(masterKdf));
            excess += blindingFactor;
        }

        return excess;
    }

    ECC::Scalar::Native SignAssetKernel(const Key::IKdf::Ptr& masterKdf,
            const CoinIDList& inputs,
            const CoinIDList& outputs,
            Key::Index assetOwnerIdx,
            TxKernelAssetControl& kernel)
    {
        auto excess = GetExcess(masterKdf, inputs, outputs);
        const auto& keypair = GetAssetOwnerKeypair(masterKdf, assetOwnerIdx);
        kernel.m_Owner = keypair.first;

        ECC::Scalar::Native kernelSk;
        masterKdf->DeriveKey(kernelSk, Key::ID(assetOwnerIdx, Key::Type::Kernel, assetOwnerIdx));
        kernel.Sign(kernelSk, keypair.second);
        kernelSk = -kernelSk;
        excess += kernelSk;

        return excess;
    }
}