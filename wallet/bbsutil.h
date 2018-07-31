#pragma once
#include "common.h"
#include "utility/io/buffer.h"

namespace beam {

namespace util {

using PrivKey = ECC::Scalar::Native;
using PubKey = ECC::Hash::Value;

uint32_t channel_from_wallet_id(const WalletID& walletID);

void gen_keypair(PrivKey& privKey, PubKey& pubKey);

bool encrypt(ByteBuffer& out, const io::SerializedMsg& in, const WalletID& pubKey);

bool decrypt(uint8_t*& out, uint32_t& size, ByteBuffer& buffer, const PrivKey& privKey);

}} //namespaces
