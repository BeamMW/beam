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

#include "tx_generator.h"

using namespace beam;
using namespace ECC;

TxGenerator::TxGenerator(Key::IKdf& kdf)
	: m_Kdf(kdf)
	, m_Offset(Zero)
{
	m_MsgTx.m_Transaction = std::make_shared<Transaction>();
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

void TxGenerator::GenerateInputInTx(Height h, Amount v, beam::Key::Type keyType, uint32_t ind)
{
	Scalar::Native key;

	Input::Ptr pInp(new Input);
	SwitchCommitment::Create(key, pInp->m_Commitment, m_Kdf, Key::IDV(v, ind, keyType));
	m_MsgTx.m_Transaction->m_vInputs.push_back(std::move(pInp));
	m_Offset += key;
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

void TxGenerator::GenerateOutputInTx(Height h, Amount v, beam::Key::Type keyType, bool isPublic, uint32_t ind)
{
	Output::Ptr pOut(new Output);
	ECC::Scalar::Native key;

	pOut->m_Incubation = 2;
	pOut->Create(key, m_Kdf, Key::ID(v, keyType, ind), isPublic);
	m_MsgTx.m_Transaction->m_vOutputs.push_back(std::move(pOut));

	key = -key;
	m_Offset += key;
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

void TxGenerator::GenerateKernel(Height h, Amount fee, uint32_t ind)
{
	TxKernel::Ptr pKrn(new TxKernel);
	Scalar::Native key;

	if (fee > 0)
		pKrn->m_Fee = fee;

	m_Kdf.DeriveKey(key, Key::ID(ind, Key::Type::Kernel));
	pKrn->m_Commitment = Point::Native(ECC::Context::get().G * key);

	ECC::Hash::Value hv;
	pKrn->get_Hash(hv);
	pKrn->m_Signature.Sign(hv, key);
	m_MsgTx.m_Transaction->m_vKernels.push_back(std::move(pKrn));

	key = -key;
	m_Offset += key;
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

void TxGenerator::GenerateKernel()
{
	TxKernel::Ptr pKrn(new TxKernel);

	m_MsgTx.m_Transaction->m_vKernels.push_back(std::move(pKrn));
}

const proto::NewTransaction& TxGenerator::GetTransaction()
{
	return m_MsgTx;
}

bool TxGenerator::IsValid() const
{
	Transaction::Context ctx;

	return m_MsgTx.m_Transaction->IsValid(ctx);
}

void TxGenerator::Sort()
{
	m_MsgTx.m_Transaction->Normalize();
}

void TxGenerator::SortInputs()
{
	std::sort(m_MsgTx.m_Transaction->m_vInputs.begin(), m_MsgTx.m_Transaction->m_vInputs.end());
}

void TxGenerator::SortOutputs()
{
	std::sort(m_MsgTx.m_Transaction->m_vOutputs.begin(), m_MsgTx.m_Transaction->m_vOutputs.end());
}

void TxGenerator::SortKernels()
{
	std::sort(m_MsgTx.m_Transaction->m_vKernels.begin(), m_MsgTx.m_Transaction->m_vKernels.end());
}

void TxGenerator::ZeroOffset()
{
	m_Offset = Zero;
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

TxGenerator::Inputs TxGenerator::GenerateInputsFromOutputs()
{
	Inputs inputs;
	const auto& outputs = GetTransaction().m_Transaction->m_vOutputs;
	inputs.resize(outputs.size());
	std::transform(outputs.begin(), outputs.end(), inputs.begin(),
		[](const Output::Ptr& output)
		{
			Input input;
			input.m_Commitment = output->m_Commitment;
			return input;
		}
	);

	return inputs;
}