#include "util.h"
#include "wallet/core/wallet.h"
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

wallet::IWalletDB::Ptr init_wallet_db(const std::string& path, uintBig* walletSeed, io::Reactor::Ptr reactor) {
    static const std::string TEST_PASSWORD("12321");

    if (boost::filesystem::exists(path)) boost::filesystem::remove_all(path);

    std::string password(TEST_PASSWORD);
    password += path;

    NoLeak<uintBig> seed;
    Hash::Value hv;
    Hash::Processor() << password >> hv;
    seed.V = hv;

    auto walletDB = wallet::WalletDB::init(path, password, seed, reactor);

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
