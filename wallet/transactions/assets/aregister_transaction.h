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

#include <condition_variable>
#include <boost/optional.hpp>
#include "asset_base_tx.h"

namespace beam::wallet
{
    class AssetRegisterTransaction : public AssetTransaction
    {
    public:
        class Creator : public BaseTransaction::Creator
        {
        public:
            Creator() = default;
        private:
            BaseTransaction::Ptr Create(const TxContext& context) override;
            TxParameters CheckAndCompleteParameters(const TxParameters& p) override;
        };

    private:
        AssetRegisterTransaction(const TxContext& context);
        bool IsInSafety() const override;

        void UpdateImpl() override;

        enum State : uint8_t
        {
            Initial,
            Making,
            Registration,
            KernelConfirmation,
            AssetConfirmation,
            AssetCheck,
            Finalizing
        };

    private:
        struct MyBuilder;
        std::shared_ptr<MyBuilder> _builder;
    };
}
