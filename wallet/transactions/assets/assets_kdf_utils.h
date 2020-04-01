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
#include "block_crypt.h"
#include "wallet/core/wallet_db.h"

namespace beam::wallet
{
    beam::PeerID GetAssetOwnerID(const Key::IKdf::Ptr& masterKdf, const std::string& meta);
    std::vector<Input::Ptr> GenerateAssetInputs(const Key::IKdf::Ptr& masterKdf, const wallet::CoinIDList& coins);
    std::vector<Output::Ptr> GenerateAssetOutputs(const Key::IKdf::Ptr& masterKdf, Height minHeight, const CoinIDList& coins);

    ECC::Scalar::Native SignAssetKernel(const Key::IKdf::Ptr& masterKdf,
            const CoinIDList& inputs,
            const CoinIDList& outputs,
            const std::string& meta,
            TxKernelAssetControl& kernel);
}
