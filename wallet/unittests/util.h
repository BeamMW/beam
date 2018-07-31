#pragma once
#include "wallet/wallet_db.h"

namespace beam {

IKeyChain::Ptr init_keychain(const std::string& path, const ECC::Hash::Value& pubKey, const ECC::Scalar::Native& privKey, bool genTreasury);

} //namespace

std::ostream& operator<<(std::ostream& os, const ECC::Scalar::Native& sn);
