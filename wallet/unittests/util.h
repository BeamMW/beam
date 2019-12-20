#pragma once
#include "wallet/core/wallet_db.h"

namespace beam {

wallet::IWalletDB::Ptr init_wallet_db(const std::string& path, ECC::uintBig* walletSeed, io::Reactor::Ptr reactor);

} //namespace

std::ostream& operator<<(std::ostream& os, const ECC::Scalar::Native& sn);
