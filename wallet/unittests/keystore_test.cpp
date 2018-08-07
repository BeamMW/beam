#include "wallet/keystore.h"
#include "utility/logger.h"
#include <boost/filesystem.hpp>

namespace {

const std::string KEYSTORE_FILE("test_keystore_file");
const char PASSWORD[] = "123456";

struct FileCleanup {
    FileCleanup() { boost::filesystem::remove_all(KEYSTORE_FILE); }
    ~FileCleanup() { boost::filesystem::remove_all(KEYSTORE_FILE); }
};

} //namespace

int keystore_test_normal() {
    using namespace beam;

    int failures=0;

    IKeyStore::Options options;
    options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
    options.fileName = KEYSTORE_FILE;
    IKeyStore::Ptr ks = IKeyStore::create(options, PASSWORD, sizeof(PASSWORD));

    PubKey key1, key2, key3;
    ks->gen_keypair(key1, PASSWORD, sizeof(PASSWORD), true);
    ks->gen_keypair(key2, PASSWORD, sizeof(PASSWORD), true);
    ks->gen_keypair(key3, PASSWORD, sizeof(PASSWORD), true);
    LOG_DEBUG() << "generated keypairs, " << key1 << " " << key2 << " " << key3;



    return failures;
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
