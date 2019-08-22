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

#include "trezor_key_keeper.h"
#include "core/block_rw.h"
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    TrezorKeyKeeper::TrezorKeyKeeper()
        : m_latestSlot(0)
    {

    }

    TrezorKeyKeeper::~TrezorKeyKeeper()
    {

    }

    Key::IPKdf::Ptr TrezorKeyKeeper::get_OwnerKdf() const
    {
        auto key = m_hwWallet.getOwnerKeySync();

        // TODO: temporary PIN to decrypt owner key, should be removed
        std::string pass = "1";

        KeyString ks;
        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));

        ks.m_sRes = key;

        std::shared_ptr<ECC::HKdfPub> pKdf = std::make_shared<ECC::HKdfPub>();

        if (!ks.Import(*pKdf))
        {
            LOG_ERROR() << "veiw key import failed";
        }

        return pKdf;
    }

    Key::IKdf::Ptr TrezorKeyKeeper::get_SbbsKdf() const
    {
        // !TODO: temporary solution to init SBBS KDF with commitment
        Key::IDV kidv{ 0, 0, Key::Type::Regular };
        auto key = m_hwWallet.generateKeySync(kidv, true);

        Key::IKdf::Ptr sbbsKdf;

        ECC::HKdf::Create(sbbsKdf, key.m_X);

        return sbbsKdf;
    }

    void TrezorKeyKeeper::GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {

    }

    void TrezorKeyKeeper::GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        using namespace std;

        auto thisHolder = shared_from_this();
        shared_ptr<Outputs> result = make_shared<Outputs>();
        shared_ptr<exception> storedException;
        shared_ptr<future<void>> futureHolder = make_shared<future<void>>();
        *futureHolder = do_thread_async(
            [thisHolder, this, schemeHeight, ids, result, storedException]()
            {
                try
                {
                    *result = GenerateOutputsSync(schemeHeight, ids);
                }
                catch (const exception& ex)
                {
                    *storedException = ex;
                }
            },
            [futureHolder, resultCallback = move(resultCallback), exceptionCallback = move(exceptionCallback), result, storedException]() mutable
            {
                if (storedException)
                {
                    exceptionCallback(*storedException);
                }
                else
                {
                    resultCallback(move(*result));
                }
                futureHolder.reset();
            });
    }

    size_t TrezorKeyKeeper::AllocateNonceSlot()
    {
        m_latestSlot++;
        m_hwWallet.generateNonceSync((uint8_t)m_latestSlot);
        return m_latestSlot;
    }

    IPrivateKeyKeeper::PublicKeys TrezorKeyKeeper::GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey)
    {
        PublicKeys result;
        result.reserve(ids.size());

        for (const auto& idv : ids)
        {
            ECC::Point& publicKey = result.emplace_back();
            publicKey = m_hwWallet.generateKeySync(idv, createCoinKey);
        }

        return result;
    }

    ECC::Point TrezorKeyKeeper::GeneratePublicKeySync(const Key::IDV& id, bool createCoinKey)
    {
        return m_hwWallet.generateKeySync(id, createCoinKey);
    }

    IPrivateKeyKeeper::Outputs TrezorKeyKeeper::GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids)
    {
        Outputs outputs;
        outputs.reserve(ids.size());

        for (const auto& kidv : ids)
        {
            auto& output = outputs.emplace_back(std::make_unique<Output>());
            output->m_Commitment = m_hwWallet.generateKeySync(kidv, true);

            output->m_pConfidential.reset(new ECC::RangeProof::Confidential);
            *output->m_pConfidential = m_hwWallet.generateRangeProofSync(kidv, false);
        }

        return outputs;
    }

    ECC::Point TrezorKeyKeeper::GenerateNonceSync(size_t slot)
    {
        assert(m_latestSlot >= slot);
        return m_hwWallet.getNoncePublicSync((uint8_t)slot);
    }

    ECC::Scalar TrezorKeyKeeper::SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const ECC::Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParamerters, const ECC::Point::Native& publicNonce)
    {
        HWWallet::TxData txData;
        txData.fee = kernelParamerters.fee;
        txData.height = kernelParamerters.height;
        txData.kernelCommitment = kernelParamerters.commitment;
        txData.kernelNonce = publicNonce;
        txData.nonceSlot = (uint32_t)nonceSlot;
        txData.offset = offset;

        return m_hwWallet.signTransactionSync(inputs, outputs, txData);
    }

}