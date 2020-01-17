// Copyright 2020 The Beam Team
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

#include "pull_tx_builder.h"

#include "core/shielded.h"

namespace ECC {

	void GenerateRandom(void* p, uint32_t n)
	{
		for (uint32_t i = 0; i < n; i++)
			((uint8_t*)p)[i] = (uint8_t)rand();
	}

	void SetRandom(uintBig& x)
	{
		GenerateRandom(x.m_pData, x.nBytes);
	}

	void SetRandom(Scalar::Native& x)
	{
		Scalar s;
		while (true)
		{
			SetRandom(s.m_Value);
			if (!x.Import(s))
				break;
		}
	}
}


namespace beam::wallet::lelantus
{
    PullTxBuilder::PullTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amount, Amount fee)
        : BaseTxBuilder(tx, subTxID, amount, fee)
    {

    }

    Transaction::Ptr PullTxBuilder::CreateTransaction(const std::vector<ECC::Point::Storage>& shieldedList, TxoID startIndex, TxoID shieldedIndex, const ECC::Scalar::Native& witnessSk, const ECC::Scalar::Native& spendKey)
    {
        // create transaction
        auto transaction = std::make_shared<Transaction>();

        transaction->m_vOutputs = move(m_Outputs);

        ECC::Scalar::Native outputSk;
        {
            ECC::Scalar::Native offset = Zero;

            ECC::Point comm;
            SwitchCommitment().Create(outputSk, comm, *m_Tx.GetWalletDB()->get_MasterKdf(), m_OutputCoins[0]);

            offset = -outputSk;

            transaction->m_Offset = offset;
        }

        {
			TxoID windowEnd = startIndex + shieldedList.size();
			Lelantus::Cfg cfg;
            TxKernelShieldedInput::Ptr pKrn(new TxKernelShieldedInput);

			pKrn->m_Fee = GetFee();
            pKrn->m_Height.m_Min = m_Tx.GetWalletDB()->getCurrentHeight();
            pKrn->m_WindowEnd = windowEnd;
            pKrn->m_SpendProof.m_Cfg = cfg;

			Lelantus::CmListVec lst;

			//assert(nWnd1 <= m_Shielded.m_Wnd0 + m_Shielded.m_N);
			if (windowEnd == startIndex + cfg.get_N())
			{
				lst.m_vec.resize(shieldedList.size());
				std::copy(shieldedList.begin(), shieldedList.end(), lst.m_vec.begin());
			}
			else
			{
				// zero-pad from left
				lst.m_vec.resize(cfg.get_N());
				for (size_t i = 0; i < cfg.get_N() - shieldedList.size(); i++)
				{
					ECC::Point::Storage& v = lst.m_vec[i];
					v.m_X = Zero;
					v.m_Y = Zero;
				}
				std::copy(shieldedList.begin(), shieldedList.end(), lst.m_vec.end() - shieldedList.size());
			}

			ECC::Scalar::Native inputSk;
			ECC::SetRandom(inputSk);

			assert(shieldedIndex < windowEnd && shieldedIndex >= startIndex);
			uint32_t l = static_cast<uint32_t>(cfg.get_N() - (shieldedIndex - startIndex) - 1);
			Lelantus::Prover p(lst, pKrn->m_SpendProof);
			//p.m_Witness.V.m_L = static_cast<uint32_t>(m_Shielded.m_N - m_Shielded.m_Confirmed) - 1;
			p.m_Witness.V.m_L = l;
			p.m_Witness.V.m_R = witnessSk;
			p.m_Witness.V.m_R_Output = inputSk;
			p.m_Witness.V.m_SpendSk = spendKey;
			p.m_Witness.V.m_V = GetAmount() + GetFee();

			pKrn->UpdateMsg();

			ECC::Oracle o1;
			o1 << pKrn->m_Msg;
			p.Generate(Zero, o1);

			{
				ECC::InnerProduct::BatchContextEx<4> bc;
				std::vector<ECC::Scalar::Native> vKs;
				vKs.resize(cfg.get_N());
				ECC::Oracle o2;
				if (!p.m_Proof.IsValid(bc, o2, &vKs.front()))
				{
					return nullptr;
				}
			}

			pKrn->MsgToID();

			//m_Shielded.m_SpendPk = pKrn->m_SpendProof.m_SpendPk;
			ECC::Point::Native pt;
			if (!pKrn->IsValid(m_Tx.GetWalletDB()->getCurrentHeight(), pt))
			{
				return nullptr;
			}
			
            transaction->m_vKernels.push_back(std::move(pKrn));

            ECC::Scalar::Native offset = transaction->m_Offset;
            offset += inputSk;
            transaction->m_Offset = offset;
        }

        transaction->Normalize();

        return transaction;
    }
} // namespace beam::wallet::lelantus