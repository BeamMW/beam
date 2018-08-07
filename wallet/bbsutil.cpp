#include "bbsutil.h"
#include "core/proto.h"


namespace beam { namespace util {

uint32_t channel_from_wallet_id(const WalletID& walletID) {
    // TODO 8 channels for now
    return walletID.m_pData[0] / 8;
}

using Nonce = ECC::Scalar::Native;

static void gen_nonce(Nonce& nonce) {
    ECC::Scalar sc;
    uint64_t seed;

    // here we want to read as little as possible from slow sources, TODO: review this
    ECC::GenRandom(&seed, 8);
    ECC::Hash::Processor() << seed >> sc.m_Value;

    nonce.Import(sc);
}

static void gen_test_nonce(Nonce& nonce, uint64_t seed) {
    ECC::Scalar sc;
    ECC::Hash::Processor() << seed >> sc.m_Value;

    nonce.Import(sc);
}

bool encrypt(ByteBuffer& out, const io::SerializedMsg& in, const WalletID& pubKey) {
    Nonce nonce;
    gen_nonce(nonce);
    io::SharedBuffer msg = io::normalize(in, false);
    return proto::BbsEncrypt(out, pubKey, nonce, msg.data, msg.size);
}

bool decrypt(uint8_t*& out, uint32_t& size, ByteBuffer& buffer, const PrivKey& privKey) {
    out = &buffer.at(0);
	size = buffer.size();
    return proto::BbsDecrypt(out, size, (PrivKey&)privKey);
}

void gen_keypair(PrivKey& privKey, PubKey& pubKey) {
    gen_nonce(privKey);
    proto::Sk2Pk(pubKey, privKey);
}

void gen_test_keypair(PrivKey& privKey, PubKey& pubKey, uint64_t seed) {
    gen_test_nonce(privKey, seed);
    proto::Sk2Pk(pubKey, privKey);
}


}} //namespaces


