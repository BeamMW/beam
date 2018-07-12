#include "tx_generator.h"

using namespace beam;
using namespace ECC;

TxGenerator::TxGenerator(const Kdf& kdf)
	: m_Kdf(kdf)
	, m_Offset(Zero)
{
	m_MsgTx.m_Transaction = std::make_shared<Transaction>();
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

void TxGenerator::GenerateInputInTx(Height h, Amount v)
{
	Scalar::Native key;
	DeriveKey(key, m_Kdf, h, KeyType::Coinbase);

	Input::Ptr pInp(new Input);
	pInp->m_Commitment = ECC::Commitment(key, v);
	m_MsgTx.m_Transaction->m_vInputs.push_back(std::move(pInp));
	m_Offset += key;
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

void TxGenerator::GenerateOutputInTx(Height h, Amount v)
{
	Output::Ptr pOut(new Output);
	ECC::Scalar::Native key;

	DeriveKey(key, m_Kdf, h, KeyType::Regular);
	pOut->m_Incubation = 2;
	pOut->Create(key, v, true);
	m_MsgTx.m_Transaction->m_vOutputs.push_back(std::move(pOut));

	key = -key;
	m_Offset += key;
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

void TxGenerator::GenerateKernel(Height h, Amount fee)
{
	TxKernel::Ptr pKrn(new TxKernel);
	Scalar::Native key;

	if (fee > 0)
		pKrn->m_Fee = fee;

	DeriveKey(key, m_Kdf, h, KeyType::Kernel);
	pKrn->m_Excess = Point::Native(ECC::Context::get().G * key);

	ECC::Hash::Value hv;
	pKrn->get_HashForSigning(hv);
	pKrn->m_Signature.Sign(hv, key);
	m_MsgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

	key = -key;
	m_Offset += key;
	m_MsgTx.m_Transaction->m_Offset = m_Offset;
}

void TxGenerator::GenerateKernel()
{
	TxKernel::Ptr pKrn(new TxKernel);

	m_MsgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));
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
	m_MsgTx.m_Transaction->Sort();
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
	std::sort(m_MsgTx.m_Transaction->m_vKernelsOutput.begin(), m_MsgTx.m_Transaction->m_vKernelsOutput.end());
}