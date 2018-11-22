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
	IWalletDB* m_pWalletDB;

	std::vector<Block::Body> m_vBlocks;
	ECC::Scalar::Native m_Offset;

	std::vector<Coin> m_Coins;
	std::vector<Height> m_vIncubation;

	std::mutex m_Mutex;
	std::vector<std::thread> m_vThreads;

	Block::Body& get_WriteBlock();
	void FinishLastBlock();
	int Generate(uint32_t nCount, Height dh, Amount v);
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

int TreasuryBlockGenerator::Generate(uint32_t nCount, Height dh, Amount v)
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
	m_vIncubation.resize(nCount);

	Height h = 0;

	for (uint32_t i = 0; i < nCount; i++, h += dh)
	{
		Coin& coin = m_Coins[i];
		coin.m_ID.m_Type = Key::Type::Regular;
		coin.m_ID.m_Value = v;
		coin.m_status = Coin::Maturing;
		coin.m_createHeight = h + Rules::HeightGenesis;

		m_vIncubation[i] = h;
	}

	m_pWalletDB->store(m_Coins); // we get coin id only after store

	m_vThreads.resize(std::thread::hardware_concurrency());
	assert(!m_vThreads.empty());

	for (uint32_t i = 0; i < m_vThreads.size(); i++)
		m_vThreads[i] = std::thread(&TreasuryBlockGenerator::Proceed, this, i);

	for (uint32_t i = 0; i < m_vThreads.size(); i++)
		m_vThreads[i].join();

	// at least 1 kernel
	{
		Coin::ID cid;
		cid.m_Type = Key::Type::Kernel;
		cid.m_Idx = m_pWalletDB->AllocateKidRange(1);

		ECC::Scalar::Native k = m_pWalletDB->calcKey(cid);

		TxKernel::Ptr pKrn(new TxKernel);
		pKrn->m_Commitment = ECC::Point::Native(Context::get().G * k);

		Merkle::Hash hv;
		pKrn->get_Hash(hv);
		pKrn->m_Signature.Sign(hv, k);

		get_WriteBlock().m_vKernels.push_back(std::move(pKrn));
		m_Offset += k;
	}

	FinishLastBlock();

	for (auto i = 0u; i < m_vBlocks.size(); i++)
	{
		m_vBlocks[i].Normalize();
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
		m_Offset = Zero;
	}
	return m_vBlocks.back();
}

void TreasuryBlockGenerator::Proceed(uint32_t i0)
{
	std::vector<Output::Ptr> vOut;

	std::vector<ECC::Scalar::Native> vSk;

	for (size_t i = i0; i < m_Coins.size(); i += m_vThreads.size())
	{
		const Coin& coin = m_Coins[i];

		Output::Ptr pOutp(new Output);
		pOutp->m_Incubation = m_vIncubation[i];

		vSk.resize(vSk.size() + 1);

		ECC::Scalar::Native& sk = vSk.back();;
		pOutp->Create(sk, *m_pWalletDB->get_ChildKdf(coin.m_ID.m_iChild), coin.m_ID);

		vOut.push_back(std::move(pOutp));
	}

	std::unique_lock<std::mutex> scope(m_Mutex);

	size_t iOutp = 0;
	for (size_t i = i0; i < m_Coins.size(); i += m_vThreads.size(), iOutp++)
	{
		Block::Body& block = get_WriteBlock();

		block.m_vOutputs.push_back(std::move(vOut[iOutp]));
		m_Offset += vSk[iOutp];
		block.m_Subsidy += m_Coins[i].m_ID.m_Value;
	}
}

int GenerateTreasury(IWalletDB* pWalletDB, const std::string& sPath, uint32_t nCount, Height dh, Amount v)
{
	TreasuryBlockGenerator tbg;
	tbg.m_sPath = sPath;
	tbg.m_pWalletDB = pWalletDB;

	return tbg.Generate(nCount, dh, v);
}


IWalletDB::Ptr init_wallet_db(const std::string& path, uintBig* walletSeed) {
    static const std::string TEST_PASSWORD("12321");

    if (boost::filesystem::exists(path)) boost::filesystem::remove_all(path);

    std::string password(TEST_PASSWORD);
    password += path;

    NoLeak<uintBig> seed;
    Hash::Value hv;
    Hash::Processor() << password >> hv;
    seed.V = hv;

    auto walletDB = WalletDB::init(path, password, seed);

    if (walletSeed) {
        TreasuryBlockGenerator tbg;
        tbg.m_sPath = path + "_";
        tbg.m_pWalletDB = walletDB.get();
		Height dh = 1;
		uint32_t nCount = 10;
        tbg.Generate(nCount, dh, Rules::Coin * 10);
        *walletSeed = seed.V;
    }

    return walletDB;
}

} //namespace

std::ostream& operator<<(std::ostream& os, const ECC::Scalar::Native& sn) {
    Scalar s;
    sn.Export(s);
    os << s.m_Value;
    return os;
}
