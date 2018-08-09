#pragma once
#include "wallet/wallet_db.h"

namespace beam {

IKeyChain::Ptr init_keychain(const std::string& path, ECC::uintBig* walletSeed);

bool ReadTreasury(std::vector<Block::Body>& vBlocks, const std::string& sPath);

} //namespace

std::ostream& operator<<(std::ostream& os, const ECC::Scalar::Native& sn);
