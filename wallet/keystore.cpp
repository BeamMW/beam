#include "keystore.h"
#include "core/proto.h"
#include <boost/filesystem.hpp>
#include <stdexcept>
#include <stdio.h>

namespace beam {

namespace {

using PrivKey = ECC::Scalar::Native;
using KeyPairs = std::map<PubKey, ECC::NoLeak<PrivKey>>;
using Nonce = ECC::Scalar::Native;
using PasswordHash = ECC::NoLeak<ECC::Hash::Value>;

void gen_nonce(Nonce& nonce) {
    ECC::Scalar sc;
    uint64_t seed;

    // here we want to read as little as possible from slow sources, TODO: review this
    ECC::GenRandom(&seed, 8);
    ECC::Hash::Processor() << seed >> sc.m_Value;

    nonce.Import(sc);
}

void hash_from_password(PasswordHash& out, const void* password, size_t passwordLen) {
    ECC::NoLeak<ECC::Hash::Processor> hp;
	hp.V.Write(password, passwordLen);
    hp.V >> out.V;
}

void aes_decrypt(void* buffer, size_t bufferLen, const PasswordHash& key) {
    AES::Encoder enc;
    enc.Init(key.V.m_pData);
    AES::Decoder dec;
    dec.Init(enc);
    uint8_t* p = (uint8_t*)buffer;
    uint8_t* end = p + bufferLen;
    for (; p<end; p+=AES::s_BlockSize) {
        dec.Proceed(p, p);
    }
}

void aes_encrypt(void* buffer, size_t bufferLen, const PasswordHash& key) {
    AES::Encoder enc;
    enc.Init(key.V.m_pData);
    uint8_t* p = (uint8_t*)buffer;
    uint8_t* end = p + bufferLen;
    for (; p<end; p+=AES::s_BlockSize) {
        enc.Proceed(p, p);
    }
}

struct AutoClose {
    FILE* f=0;
    ~AutoClose() { if(f) fclose(f); }
};

void xor_32_bytes(uint8_t* out, const uint8_t* mask) {
    for (int i=0; i<32; ++i) {
        *(out++) ^= *(mask++);
    }
}

void read_keystore_file(KeyPairs& out, const std::string& fileName, const PasswordHash& key) {
    AutoClose a;

#ifdef WIN32
    a.f = _wfopen(Utf8toUtf16(fileName.c_str()).c_str(), L"a+b");
#else
    a.f = fopen(fileName.c_str(), "a+b");
#endif

    if (!a.f) {
        throw KeyStoreException(std::string("keystore: cannot open file ") + fileName);
    }

    fseek(a.f, 0, SEEK_END);
    size_t size = ftell(a.f);

    if (size == 0) {
        return;
    }

    static const size_t MAX_FILE_SIZE = 64*2000;

    if ((size % 64) != 0 || size > MAX_FILE_SIZE) {
        fclose(a.f);
        throw KeyStoreException(std::string("keystore: invalid file size: ") + fileName);
    }

    void* buffer = alloca(size);

    rewind(a.f);
    auto bytesRead = fread(buffer, 1, size, a.f);
    if (bytesRead != size) {
        throw KeyStoreException(std::string("keystore: file read error: ") + fileName);
    }

    aes_decrypt(buffer, size, key);

    uint8_t* p = (uint8_t*)buffer;
    const uint8_t* end = p + size;
    PubKey pubKey;
    for (; p<end; p += 64) {
        xor_32_bytes(p, p + 32);
        memcpy(pubKey.m_pData, p, 32);
        memcpy(&(out[pubKey].V), p+32, 32);
    }
}

void write_keystore_file(const KeyPairs& in, const std::string& fileName, const PasswordHash& key) {
    std::string newFileName = fileName + ".new";

    size_t size = in.size() * 64;

    {
        AutoClose a;
#ifdef WIN32
        a.f = _wfopen(Utf8toUtf16(newFileName.c_str()).c_str(), L"w+b");
#else
        a.f = fopen(newFileName.c_str(), "w+b");
#endif
        if (!a.f) {
            throw KeyStoreException(std::string("keystore: cannot open file ") + newFileName);
        }

        if (size == 0)
            return;

        void* buffer = alloca(size);

        uint8_t* p = (uint8_t*)buffer;
        for (const auto& kp : in) {
            memcpy(p, kp.first.m_pData, 32);
            memcpy(p + 32, &(kp.second.V), 32);
            xor_32_bytes(p, p + 32);
            p += 64;
        }

        aes_encrypt(buffer, size, key);

        if (size != fwrite(buffer, 1, size, a.f)) {
            throw KeyStoreException(std::string("keystore: cannot write file ") + newFileName);
        }
    }

#ifdef WIN32
    boost::filesystem::rename(Utf8toUtf16(newFileName.c_str()), Utf8toUtf16(fileName.c_str()));
#else
    boost::filesystem::rename(newFileName, fileName);
#endif
}

} //namespace

class LocalFileKeystore : public IKeyStore {
public:
    LocalFileKeystore(const IKeyStore::Options& options, const void* password, size_t passwordLen) :
        _fileName(options.fileName)
    {
        hash_from_password(_pass, password, passwordLen);
        bool allEnabled = (options.flags & Options::enable_all_keys) != 0;
        if (allEnabled) {
            read_keystore_file_and_check();
        } else {
            enable_keys(options.enableKeys);
        }
    }

private:
    void read_keystore_file_and_check() {
        read_keystore_file(_keyPairs, _fileName, _pass);
        if (_keyPairs.empty()) return;
        static const char data[] = "xoxoxoxo";
        ByteBuffer buf;
        std::string errorMsg(std::string("keystore: file corrupted: ") + _fileName);
        if (!encrypt(buf, data, sizeof(data), _keyPairs.begin()->first)) {
            throw KeyStoreException(errorMsg);
        }
        uint8_t* out=0;
        uint32_t size=0;
        if (
            !decrypt(out, size, buf, _keyPairs.begin()->first) ||
            size != sizeof(data) ||
            memcmp(data, out, size) != 0
        ) {
            throw KeyStoreException(errorMsg);
        }
    }

    void gen_keypair(PubKey& pubKey) override {
        ECC::NoLeak<PrivKey> privKey;
        gen_nonce(privKey.V);
        proto::Sk2Pk(pubKey, privKey.V);
        memcpy(&(_unsaved[pubKey].V), &privKey.V, 32);
    }

    void save_keypair(const PubKey& pubKey, bool enable) override {
        auto it = _unsaved.find(pubKey);
        if (it == _unsaved.end()) {
            return;
        }
        memcpy(&(_keyPairs[pubKey].V), &(it->second.V), 32);
        write_keystore_file(_keyPairs, _fileName, _pass);
        if (!enable) {
            _keyPairs.erase(pubKey);
        }
        _unsaved.erase(it);
    }

    size_t size() override {
        return _keyPairs.size();
    }

    void change_password(const void* password, size_t passwordLen) override {
        KeyPairs savedKeys;
        read_keystore_file(savedKeys, _fileName, _pass);
        hash_from_password(_pass, password, passwordLen);
        write_keystore_file(savedKeys, _fileName, _pass);
    }

    void get_enabled_keys(std::set<PubKey>& enabledKeys) override {
        enabledKeys.clear();
        for (const auto& p : _keyPairs) {
            enabledKeys.insert(p.first);
        }
    }

    void enable_keys(const std::set<PubKey>& enableKeys) override {
        _keyPairs.clear();
        if (enableKeys.empty())
            return;
        read_keystore_file_and_check();
        if (_keyPairs.empty())
            return;
        std::set<PubKey> toBeErased;
        for (const auto& p : _keyPairs) {
            if (enableKeys.count(p.first) == 0) {
                toBeErased.insert(p.first);
            }
        }
        for (const auto& k : toBeErased) {
            _keyPairs.erase(k);
        }
    }

    void disable_key(const PubKey& pubKey) override {
        _keyPairs.erase(pubKey);
    }

    void erase_key(const PubKey& pubKey) override {
        size_t s = _keyPairs.size();
        _keyPairs.erase(pubKey);
        if (s != _keyPairs.size()) {
            write_keystore_file(_keyPairs, _fileName, _pass);
        }
    }

    bool encrypt(ByteBuffer& out, const void* data, size_t size, const PubKey& pubKey) override {
        Nonce nonce;
        gen_nonce(nonce);
        return proto::BbsEncrypt(out, pubKey, nonce, data, size);
    }

    bool encrypt(ByteBuffer& out, const io::SerializedMsg& in, const PubKey& pubKey) override {
        io::SharedBuffer msg = io::normalize(in, false);
        return encrypt(out, msg.data, msg.size, pubKey);
    }

    bool decrypt(uint8_t*& out, uint32_t& size, ByteBuffer& buffer, const PubKey& pubKey) override {
        auto it = _keyPairs.find(pubKey);
        if (it == _keyPairs.end()) {
            return false;
        }
        out = &buffer.at(0);
        size = buffer.size();
        return proto::BbsDecrypt(out, size, (PrivKey&)it->second.V);
    }

    std::string _fileName;
    KeyPairs _keyPairs;
    KeyPairs _unsaved;

    // TODO: use locked in memory secure buffer
    PasswordHash _pass;
};

IKeyStore::Ptr IKeyStore::create(const IKeyStore::Options& options, const void* password, size_t passwordLen) {
    static const std::string errMsgPrefix("keystore create: ");

    Ptr ptr;

    if (options.flags & Options::local_file) {
        if (options.fileName.empty() || passwordLen == 0) {
            throw KeyStoreException(errMsgPrefix + "empty file name or key");
        }
        ptr = std::make_shared<LocalFileKeystore>(options, password, passwordLen);

    } else {
        throw KeyStoreException(errMsgPrefix + "invalid options");
    }

    return ptr;
}

} //namespace
