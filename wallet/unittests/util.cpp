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

bool ReadTreasury(ByteBuffer& bb, const string& sPath)
{
	if (sPath.empty())
		return false;

	std::FStream f;
	if (!f.Open(sPath.c_str(), true))
		return false;

	size_t nSize = static_cast<size_t>(f.get_Remaining());
	if (!nSize)
		return false;

	bb.resize(f.get_Remaining());
	return f.read(&bb.front(), nSize) == nSize;
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

    if (walletSeed)
        *walletSeed = seed.V;

    return walletDB;
}

} //namespace

std::ostream& operator<<(std::ostream& os, const ECC::Scalar::Native& sn) {
    Scalar s;
    sn.Export(s);
    os << s.m_Value;
    return os;
}
