#pragma once
#include "wallet/wallet_db.h"

namespace beam {

IWalletDB::Ptr init_wallet_db(const std::string& path, ECC::uintBig* walletSeed);

bool ReadTreasury(std::vector<Block::Body>& vBlocks, const std::string& sPath);
int GenerateTreasury(IWalletDB*, const std::string& sPath, uint32_t nCount, Height dh, Amount v);

} //namespace

std::ostream& operator<<(std::ostream& os, const ECC::Scalar::Native& sn);
