#pragma once
#include "wallet/wallet_db.h"

namespace beam {

IWalletDB::Ptr init_wallet_db(const std::string& path, ECC::uintBig* walletSeed);

bool ReadTreasury(ByteBuffer&, const std::string& sPath);

} //namespace

std::ostream& operator<<(std::ostream& os, const ECC::Scalar::Native& sn);
