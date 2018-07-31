#include "util.h"
#include "wallet/wallet.h"
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "utility/logger.h"
#include <boost/filesystem.hpp>
#include <iterator>
#include <future>

using namespace std;
using namespace beam;
using namespace ECC;

namespace beam {

struct TreasuryBlockGenerator
{
	std::string m_sPath;
	IKeyChain* m_pKeyChain;

	std::vector<Block::Body> m_vBlocks;
	ECC::Scalar::Native m_Offset;

	std::vector<Coin> m_Coins;
	std::vector<std::pair<Height, ECC::Scalar::Native> > m_vIncubationAndKeys;

	std::mutex m_Mutex;
	std::vector<std::thread> m_vThreads;

	Block::Body& get_WriteBlock();
	void FinishLastBlock();
	int Generate(uint32_t nCount, Height dh);
private:
	void Proceed(uint32_t i);
};

bool ReadTreasury(std::vector<Block::Body>& vBlocks, const string& sPath)
    {
		if (sPath.empty())
			return false;

		std::FStream f;
		if (!f.Open(sPath.c_str(), true))
			return false;

		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(f);
        arc & vBlocks;

		return true;
    }

int TreasuryBlockGenerator::Generate(uint32_t nCount, Height dh)
{
	if (m_sPath.empty())
	{
		LOG_ERROR() << "Treasury block path not specified";
		return -1;
	}

	boost::filesystem::path path{ m_sPath };
	boost::filesystem::path dir = path.parent_path();
	if (!dir.empty() && !boost::filesystem::exists(dir) && !boost::filesystem::create_directory(dir))
	{
		LOG_ERROR() << "Failed to create directory: " << dir.c_str();
		return -1;
	}

	if (ReadTreasury(m_vBlocks, m_sPath))
		LOG_INFO() << "Treasury already contains " << m_vBlocks.size() << " blocks, appending.";

	if (!m_vBlocks.empty())
	{
		m_Offset = m_vBlocks.back().m_Offset;
		m_Offset = -m_Offset;
	}

	LOG_INFO() << "Generating coins...";

	m_Coins.resize(nCount);
	m_vIncubationAndKeys.resize(nCount);

	Height h = 0;

	for (uint32_t i = 0; i < nCount; i++, h += dh)
	{
		Coin& coin = m_Coins[i];
		coin.m_key_type = KeyType::Regular;
		coin.m_amount = Rules::Coin * 10;
		coin.m_status = Coin::Unconfirmed;
		coin.m_createHeight = h + Rules::HeightGenesis;


		m_vIncubationAndKeys[i].first = h;
	}

	m_pKeyChain->store(m_Coins); // we get coin id only after store

	for (uint32_t i = 0; i < nCount; ++i)
        m_vIncubationAndKeys[i].second = m_pKeyChain->calcKey(m_Coins[i]);

	m_vThreads.resize(std::thread::hardware_concurrency());
	assert(!m_vThreads.empty());

	for (uint32_t i = 0; i < m_vThreads.size(); i++)
		m_vThreads[i] = std::thread(&TreasuryBlockGenerator::Proceed, this, i);

	for (uint32_t i = 0; i < m_vThreads.size(); i++)
		m_vThreads[i].join();

	// at least 1 kernel
	{
		Coin dummy; // not a coin actually
		dummy.m_key_type = KeyType::Kernel;
		dummy.m_status = Coin::Unconfirmed;

		ECC::Scalar::Native k = m_pKeyChain->calcKey(dummy);

		TxKernel::Ptr pKrn(new TxKernel);
		pKrn->m_Excess = ECC::Point::Native(Context::get().G * k);

		Merkle::Hash hv;
		pKrn->get_HashForSigning(hv);
		pKrn->m_Signature.Sign(hv, k);

		get_WriteBlock().m_vKernelsOutput.push_back(std::move(pKrn));
		m_Offset += k;
	}

	FinishLastBlock();

	for (auto i = 0u; i < m_vBlocks.size(); i++)
	{
		m_vBlocks[i].Sort();
		m_vBlocks[i].DeleteIntermediateOutputs();
	}

	std::FStream f;
	f.Open(m_sPath.c_str(), false, true);

	yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> arc(f);
	arc & m_vBlocks;
	f.Flush();

/*
	for (auto i = 0; i < m_vBlocks.size(); i++)
		m_vBlocks[i].IsValid(i + 1, true);
*/

	LOG_INFO() << "Done";

	return 0;
}

void TreasuryBlockGenerator::FinishLastBlock()
{
	m_Offset = -m_Offset;
	m_vBlocks.back().m_Offset = m_Offset;
}

Block::Body& TreasuryBlockGenerator::get_WriteBlock()
{
	if (m_vBlocks.empty() || m_vBlocks.back().m_vOutputs.size() >= 1000)
	{
		if (!m_vBlocks.empty())
			FinishLastBlock();

		m_vBlocks.resize(m_vBlocks.size() + 1);
		m_vBlocks.back().ZeroInit();
		m_Offset = ECC::Zero;
	}
	return m_vBlocks.back();
}

void TreasuryBlockGenerator::Proceed(uint32_t i0)
{
	std::vector<Output::Ptr> vOut;

	for (uint32_t i = i0; i < m_Coins.size(); i += m_vThreads.size())
	{
		const Coin& coin = m_Coins[i];

		Output::Ptr pOutp(new Output);
		pOutp->m_Incubation = m_vIncubationAndKeys[i].first;

		const ECC::Scalar::Native& k = m_vIncubationAndKeys[i].second;
		pOutp->Create(k, coin.m_amount);

		vOut.push_back(std::move(pOutp));
		//offset += k;
		//subBlock.m_Subsidy += coin.m_amount;
	}

	std::unique_lock<std::mutex> scope(m_Mutex);

	uint32_t iOutp = 0;
	for (uint32_t i = i0; i < m_Coins.size(); i += m_vThreads.size(), iOutp++)
	{
		Block::Body& block = get_WriteBlock();

		block.m_vOutputs.push_back(std::move(vOut[iOutp]));
		block.m_Subsidy += m_Coins[i].m_amount;
		m_Offset += m_vIncubationAndKeys[i].second;
	}
}


IKeyChain::Ptr init_keychain(const std::string& path, const ECC::Hash::Value& pubKey, const ECC::Scalar::Native& privKey, bool genTreasury) {
    static const std::string TEST_PASSWORD("12321");

    if (boost::filesystem::exists(path)) boost::filesystem::remove_all(path);

    NoLeak<uintBig> walletSeed;
    Hash::Value hv;
    Hash::Processor() << TEST_PASSWORD.c_str() >> hv;
    walletSeed.V = hv;

    auto keychain = Keychain::init(path, TEST_PASSWORD, walletSeed);

    if (genTreasury) {
        TreasuryBlockGenerator tbg;
        tbg.m_sPath = path + "_";
        tbg.m_pKeyChain = keychain.get();
		Height dh = 1;
		uint32_t nCount = 100;
        tbg.Generate(nCount, dh);
    }

    return keychain;
}

} //namespace

std::ostream& operator<<(std::ostream& os, const ECC::Scalar::Native& sn) {
    Scalar s;
    sn.Export(s);
    os << s.m_Value;
    return os;
}
