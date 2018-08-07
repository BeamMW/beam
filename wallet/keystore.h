#pragma once
#include "common.h"
#include "utility/io/buffer.h"

namespace beam {

using PubKey = ECC::Hash::Value;

struct IKeyStore {
    using Ptr = std::shared_ptr<IKeyStore>;

    struct Options {
        enum Flags { local_file=1, enable_all_keys=2 /*TODO*/ };

        int flags=0;
        std::string fileName;
        std::set<PubKey> enableKeys;
    };

    /// Creates or opens the keystore based on options
    static Ptr create(const Options& options, const void* password, size_t passwordLen);

    virtual ~IKeyStore() {}

    /// Creates a new keypair, returns the public key and stores the private key
    virtual void gen_keypair(PubKey& pubKey, const void* password, size_t passwordLen, bool enable) = 0;

    virtual void get_enabled_keys(std::set<PubKey>& enabledKeys) = 0;

    virtual void enable_keys(const std::set<PubKey>& enableKeys, const void* password, size_t passwordLen) = 0;

    // password is not needed if not erased permanently
    virtual void disable_key(const PubKey& pubKey, bool erasePermanently=false, const void* password=0, size_t passwordLen=0) = 0;

    /// Encrypts the message, returns false iff private key os not found for public key given
    virtual bool encrypt(ByteBuffer& out, const io::SerializedMsg& in, const PubKey& pubKey) = 0;

    /// In-place decrypts the message given in buffer using private key associated with pubKey.
    /// Returns false if private key is missing for pubKey or decription process fails
    virtual bool decrypt(uint8_t*& out, uint32_t& size, ByteBuffer& buffer, const PubKey& pubKey) = 0;
};

} //namespace
