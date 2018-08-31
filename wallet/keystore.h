#pragma once
#include "common.h"
#include "utility/io/buffer.h"

namespace beam {

using PubKey = ECC::Hash::Value;

class KeyStoreException : public std::runtime_error {
public:
    explicit KeyStoreException(const std::string& msg)
        : std::runtime_error(msg.c_str())
    {
    }

    explicit KeyStoreException(const char *msg)
        : std::runtime_error(msg)
    {
    }
};

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

    /// Creates a new keypair, returns the public key, use save_keypair to save and enable
    virtual void gen_keypair(PubKey& pubKey) = 0;

    /// Saves unsaved keypair and enables if needed
    virtual void save_keypair(const PubKey& pubKey, bool enable) = 0;

    /// Returns # of keypairs
    virtual size_t size() = 0;

    /// return public keys for enabled keypairs
    virtual void get_enabled_keys(std::set<PubKey>& enabledKeys) = 0;

    /// enables given keypairs only
    virtual void enable_keys(const std::set<PubKey>& enableKeys) = 0;

    /// disables the keypair but not erases from the storage
    virtual void disable_key(const PubKey& pubKey) = 0;

    /// disables keypair and erases permanently from the storage
    virtual void erase_key(const PubKey& pubKey) = 0;

    /// changes storage password
    virtual void change_password(const void* password, size_t passwordLen) = 0;

    /// Encrypts the message, returns false iff private key not found for public key given
    virtual bool encrypt(ByteBuffer& out, const void* data, size_t size, const PubKey& pubKey) = 0;

    /// Encrypts the message, returns false iff private key not found for public key given
    virtual bool encrypt(ByteBuffer& out, const io::SerializedMsg& in, const PubKey& pubKey) = 0;

    /// In-place decrypts the message given in buffer using private key associated with pubKey.
    /// Returns false if private key is missing for pubKey or decription process fails
    virtual bool decrypt(uint8_t*& out, uint32_t& size, ByteBuffer& buffer, const PubKey& pubKey) = 0;
};

} //namespace
