#pragma once

#include "beam/node.h"

class TxGenerator
{
public:
	TxGenerator(const ECC::Kdf& kdf);

	void GenerateInputInTx(beam::Height h, beam::Amount v);
	void GenerateOutputInTx(beam::Height h, beam::Amount v);
	void GenerateKernel(beam::Height h, beam::Amount fee = 0);
	void GenerateKernel();

	const beam::proto::NewTransaction& GetTransaction();
	bool IsValid() const;

	void Sort();
	void SortInputs();
	void SortOutputs();
	void SortKernels();

private:
	ECC::Kdf m_Kdf;
	beam::proto::NewTransaction m_MsgTx;
	ECC::Scalar::Native m_Offset;
};