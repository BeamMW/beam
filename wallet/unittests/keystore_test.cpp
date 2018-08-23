#include "wallet/keystore.h"
#include "wallet/secstring.h"
#include "utility/logger.h"
#include <boost/filesystem.hpp>

namespace {

const std::string KEYSTORE_FILE("test_keystore_file");
const char PASSWORD[] = "123456";

struct KeystoreCleanup {
    KeystoreCleanup() { boost::filesystem::remove_all(KEYSTORE_FILE); }
    ~KeystoreCleanup() { boost::filesystem::remove_all(KEYSTORE_FILE); }
};

} //namespace

int keystore_test_normal() {
    KeystoreCleanup c;

    static const char DATA[] = "2ieiskdjksjdy32494hfuhs;djv;a''adlkvlkale[i0irpjpojpv;zv']zmkjzcdhkjdcakjhJHGJHGGJHGHJGHJ";

    using namespace beam;

    IKeyStore::Options options;
    options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
    options.fileName = KEYSTORE_FILE;
    IKeyStore::Ptr ks = IKeyStore::create(options, PASSWORD, sizeof(PASSWORD));

    PubKey key1, key2, key3;
    ks->gen_keypair(key1);
    ks->save_keypair(key1, true);
    ks->gen_keypair(key2);
    ks->save_keypair(key2, true);
    ks->gen_keypair(key3);
    ks->save_keypair(key3, false);
    LOG_DEBUG() << "generated keypairs, " << key1 << " " << key2 << " " << key3;

    std::set<PubKey> enabledKeys;
    ks->get_enabled_keys(enabledKeys);
    if (enabledKeys.size() != 2 || !enabledKeys.count(key1) || ! enabledKeys.count(key2)) {
        LOG_ERROR() << "wrong enabled keys, size=" << enabledKeys.size();
        return 1;
    }

    ByteBuffer buf;
    uint8_t* out=0;
    uint32_t size=0;
    if (!ks->encrypt(buf, DATA, sizeof(DATA), key1)) {
        LOG_ERROR() << "cannot encrypt, #1";
        return 1;
    }
    if (!ks->decrypt(out, size, buf, key1) || size != sizeof(DATA) || memcmp(DATA, out, sizeof(DATA)) != 0) {
        LOG_ERROR() << "cannot decrypt, #1";
        return 1;
    }
    if (ks->decrypt(out, size, buf, key2)) {
        LOG_ERROR() << "decrypt with wrong key returned true";
        return 1;
    }

    buf.clear();
    if (!ks->encrypt(buf, DATA, sizeof(DATA), key2)) {
        LOG_ERROR() << "cannot encrypt, #2";
        return 1;
    }
    if (!ks->decrypt(out, size, buf, key2) || size != sizeof(DATA) || memcmp(DATA, out, sizeof(DATA)) != 0) {
        LOG_ERROR() << "cannot decrypt, #2";
        return 1;
    }

    buf.clear();
    if (!ks->encrypt(buf, DATA, sizeof(DATA), key3)) {
        LOG_ERROR() << "cannot encrypt, #3";
        return 1;
    }
    if (ks->decrypt(out, size, buf, key3)) {
        LOG_ERROR() << "decrypt with disabled key returned true";
        return 1;
    }

    enabledKeys.insert(key3);
    ks->enable_keys(enabledKeys);

    enabledKeys.clear();
    ks->get_enabled_keys(enabledKeys);
    if (enabledKeys.size() != 3) {
        LOG_ERROR() << "wrong # of enabled keys " << enabledKeys.size();
        return 1;
    }

    buf.clear();
    if (!ks->encrypt(buf, DATA, sizeof(DATA), key3)) {
        LOG_ERROR() << "cannot encrypt, #3";
        return 1;
    }
    if (!ks->decrypt(out, size, buf, key3) || size != sizeof(DATA) || memcmp(DATA, out, sizeof(DATA)) != 0) {
        LOG_ERROR() << "cannot decrypt, #3";
        return 1;
    }

    ks.reset();

    try {
        const char WRONG[] = "WRONG_PASSWORD";
        ks = IKeyStore::create(options, WRONG, sizeof(WRONG));
        LOG_ERROR() << "keystore opened with wrong password";
        return 1;
    } catch (...)
    {}

    ks = IKeyStore::create(options, PASSWORD, sizeof(PASSWORD));
    buf.clear();
    if (!ks->encrypt(buf, DATA, sizeof(DATA), key2)) {
        LOG_ERROR() << "cannot encrypt, #2";
        return 1;
    }
    if (!ks->decrypt(out, size, buf, key2) || size != sizeof(DATA) || memcmp(DATA, out, sizeof(DATA)) != 0) {
        LOG_ERROR() << "cannot decrypt, #2";
        return 1;
    }

    ks->erase_key(key2);
    ks.reset();
    ks = IKeyStore::create(options, PASSWORD, sizeof(PASSWORD));
    enabledKeys.clear();
    ks->get_enabled_keys(enabledKeys);
    if (enabledKeys.size() != 2) {
        LOG_ERROR() << "wrong # of enabled keys " << enabledKeys.size();
        return 1;
    }
    if (!enabledKeys.count(key1) || ! enabledKeys.count(key3)) {
        LOG_ERROR() << "enabled keys mismatch";
        return 1;
    }

    LOG_INFO() << __FUNCTION__ << " ok";
    return 0;
}

int main() {
    using namespace beam;

    int ret=0;

    auto logger = Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);
    ECC::InitializeContext();

    try {
        ret += keystore_test_normal();
    } catch (const std::exception& e) {
        LOG_ERROR() << e.what();
        ret = 255;
    }

    return ret;
}
