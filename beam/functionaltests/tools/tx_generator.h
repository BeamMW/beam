#pragma once

#include "beam/node.h"

class TxGenerator
{
public:
	using Inputs = std::vector<beam::Input>;
public:
	TxGenerator(const ECC::Kdf& kdf);

	void GenerateInputInTx(beam::Height h, beam::Amount v, beam::KeyType keyType = beam::KeyType::Coinbase, uint32_t ind = 0);
	void GenerateOutputInTx(beam::Height h, beam::Amount v, beam::KeyType keyType = beam::KeyType::Regular, bool isPublic = true, uint32_t ind = 0);
	void GenerateKernel(beam::Height h, beam::Amount fee = 0, uint32_t ind = 0);
	void GenerateKernel();

	const beam::proto::NewTransaction& GetTransaction();
	bool IsValid() const;

	void Sort();
	void SortInputs();
	void SortOutputs();
	void SortKernels();

	void ZeroOffset();

	Inputs GenerateInputsFromOutputs();

private:
	ECC::Kdf m_Kdf;
	beam::proto::NewTransaction m_MsgTx;
	ECC::Scalar::Native m_Offset;
};