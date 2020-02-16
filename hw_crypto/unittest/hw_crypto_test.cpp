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

#include <iostream>

extern "C" {
#include "multimac.h"
#include "oracle.h"
#include "noncegen.h"
}

#include "../core/ecc_native.h"
#include "../core/block_crypt.h"


using namespace beam;

int g_TestsFailed = 0;

const Height g_hFork = 3; // whatever

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
}

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

void GenerateRandom(void* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		((uint8_t*) p)[i] = (uint8_t) rand();
}

template <uint32_t nBytes>
void SetRandom(uintBig_t<nBytes>& x)
{
	GenerateRandom(x.m_pData, x.nBytes);
}

void SetRandom(ECC::Scalar::Native& x)
{
	ECC::Scalar s;
	while (true)
	{
		SetRandom(s.m_Value);
		if (!x.Import(s))
			break;
	}
}

void SetRandom(ECC::Point::Native& value, uint8_t y = 0)
{
    ECC::Point p;

    SetRandom(p.m_X);
    p.m_Y = y;

    while (!value.Import(p))
    {
        verify_test(value == Zero);
        p.m_X.Inc();
    }
}

template <typename T>
void SetRandomOrd(T& x)
{
	GenerateRandom(&x, sizeof(x));
}

void TestMultiMac()
{
	ECC::Mode::Scope scope(ECC::Mode::Fast);

	uint32_t bb = sizeof(ECC_Min_MultiMac_Prepared);
	uint32_t cc = sizeof(ECC_Min_MultiMac_WNaf);
	bb; cc;

	const uint32_t nBatch = 8;

	ECC_Min_MultiMac_Prepared pPrepared[nBatch];
	ECC_Min_MultiMac_WNaf pWnaf[nBatch];
	ECC_Min_MultiMac_Scalar pS[nBatch];

	ECC_Min_MultiMac_Context mmCtx;
	mmCtx.m_Count = nBatch;
	mmCtx.m_pPrep = pPrepared;
	mmCtx.m_pS = pS;
	mmCtx.m_pWnaf = pWnaf;

	ECC::MultiMac_WithBufs<1, nBatch> mm1;

	for (uint32_t iGen = 0; iGen < nBatch; iGen++)
	{
		const ECC::MultiMac::Prepared& p = ECC::Context::get().m_Ipp.m_pGen_[0][iGen];
		mm1.m_ppPrepared[iGen] = &p;

		ECC_Min_MultiMac_Prepared& trg = pPrepared[iGen];
		const ECC::MultiMac::Prepared::Fast& src = p.m_Fast;

		static_assert(_countof(trg.m_pPt) <= _countof(src.m_pPt));

		for (uint32_t j = 0; j < _countof(trg.m_pPt); j++)
			trg.m_pPt[j] = src.m_pPt[j];
	}


	for (int i = 0; i < 10; i++)
	{
		mm1.Reset();

		for (uint32_t iPt = 0; iPt < nBatch; iPt++)
		{
			ECC::Scalar::Native sk;
			SetRandom(sk);

			mm1.m_pKPrep[iPt] = sk;
			mm1.m_Prepared++;

			pS[iPt].m_pK[0] = sk.get();
		}

		ECC::Point::Native res1, res2;
		mm1.Calculate(res1);

		mmCtx.m_pRes = &res2.get_Raw();
		ECC_Min_MultiMac_Calculate(&mmCtx);

		verify_test(res1 == res2);
	}
}

void TestNonceGen()
{
	static const char szSalt[] = "my_salt";

	for (int i = 0; i < 3; i++)
	{
		ECC::Hash::Value seed;
		SetRandom(seed);

		ECC::NonceGenerator ng1(szSalt);
		ng1 << seed;

		ECC_Min_NonceGenerator ng2;
		ECC_Min_NonceGenerator_Init(&ng2, szSalt, sizeof(szSalt), seed.m_pData, seed.nBytes);

		for (int j = 0; j < 10; j++)
		{
			ECC::Scalar::Native sk1, sk2;
			ng1 >> sk1;
			ECC_Min_NonceGenerator_NextScalar(&ng2, &sk2.get_Raw());

			verify_test(sk1 == sk2);
		}
	}
}

void TestOracle()
{
	for (int i = 0; i < 3; i++)
	{
		ECC::Oracle o1;
		ECC_Min_Oracle o2;
		ECC_Min_Oracle_Init(&o2);

		for (int j = 0; j < 4; j++)
		{
			for (int k = 0; k < 3; k++)
			{
				ECC::Scalar::Native sk1, sk2;
				o1 >> sk1;
				ECC_Min_Oracle_NextScalar(&o2, &sk2.get_Raw());

				verify_test(sk1 == sk2);
			}

			ECC::Hash::Value val;
			SetRandom(val);

			o1 << val;
			ECC_Min_Oracle_Expose(&o2, val.m_pData, val.nBytes);
		}
	}
}

int main()
{
	Rules::get().CA.Enabled = true;
	Rules::get().pForks[1].m_Height = g_hFork;
	Rules::get().pForks[2].m_Height = g_hFork;

	TestMultiMac();
	TestNonceGen();
	TestOracle();

    return g_TestsFailed ? -1 : 0;
}
