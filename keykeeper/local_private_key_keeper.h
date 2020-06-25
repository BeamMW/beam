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

#pragma once

#include "wallet/core/private_key_keeper.h"
#include "wallet/core/variables_db.h"
#include <utility>

namespace beam::wallet
{
    //
    // Private key keeper in local storage implementation
    //
    class LocalPrivateKeyKeeper2
        : public PrivateKeyKeeper_AsyncNotify
    {
        static Status::Type ToImage(ECC::Point::Native& res, uint32_t iGen, const ECC::Scalar::Native& sk);
        static void UpdateOffset(Method::TxCommon&, const ECC::Scalar::Native& kDiff, const ECC::Scalar::Native& kKrn);

        struct Aggregation;

    public:

        LocalPrivateKeyKeeper2(const ECC::Key::IKdf::Ptr&);

#define THE_MACRO(method) \
        virtual Status::Type InvokeSync(Method::method& m) override;

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

    protected:

        ECC::Key::IKdf::Ptr m_pKdf;

        // make nonce generation abstract, to enable testing the code with predefined nonces
        virtual Slot::Type get_NumSlots() = 0;
        virtual void get_Nonce(ECC::Scalar::Native&, Slot::Type) = 0;
        virtual void Regenerate(Slot::Type) = 0;

        // user interaction emulation
        virtual bool IsTrustless() { return false; }
        virtual Status::Type ConfirmSpend(Amount, Asset::ID, const PeerID&, const TxKernel&, bool bFinal) { return Status::Success; }

    };

    class LocalPrivateKeyKeeperStd
        : public LocalPrivateKeyKeeper2
    {
    public:

        static const Slot::Type s_Slots = 10 * 1024 * 1024; // practically unlimited

        struct State
        {
            ECC::Hash::Value m_hvLast;

            typedef std::map<Slot::Type, ECC::Hash::Value> UsedMap;
            UsedMap m_Used;

            ECC::Hash::Value* get_At(Slot::Type, bool& bAlloc);
            ECC::Hash::Value& get_AtReady(Slot::Type);
            void Regenerate(ECC::Hash::Value&);
            void Regenerate(Slot::Type);

        } m_State;

        using LocalPrivateKeyKeeper2::LocalPrivateKeyKeeper2;

    protected:

        virtual Slot::Type get_NumSlots() override;
        virtual void get_Nonce(ECC::Scalar::Native&, Slot::Type) override;
        virtual void Regenerate(Slot::Type) override;

    };
}
