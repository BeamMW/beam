#include <storage.h>

std::ofstream create_out_filestream(const char* filename) {
    std::ofstream os;
    os.open (filename, std::ofstream::binary);
    return os;
}

// UTXO implementation
void UTXO::write(std::ofstream &os) {
	os.write(reinterpret_cast<char*>(this), SIZEUTXO);
};

void UTXO::write(std::ofstream &os, char* key) {
    char* encoded = encode(this, key);
	os.write(encoded, SIZEUTXO);
};

UTXO* UTXO::recover(std::ifstream &is, size_t offset) {
    UTXO* out = recover_from<UTXO>(is, offset);
    return out;
}

UTXO* UTXO::recover(std::ifstream &is, size_t offset, const char* key) {
    UTXO* out = recover_from<UTXO>(is, offset, key);
    return out;
}
// UTXO

void crypto_by_key(char* input, char* output, const char* key, size_t N) {

    size_t len_key = strlen(key);

    for (size_t i = 0; i < N; i++)
        output[i] = input[i] ^ key[i % len_key];
}


void decode(char* encoded, size_t data_size, const char* key) {
    crypto_by_key(encoded, encoded, key, data_size);
}

// For testing
std::string crypto(const std::string& data, const std::string& key) {

	std::string coded = data;

	for (int i = 0; i < coded.length(); i++)
	      coded[i] = data[i] ^ key[i % key.length()];

	return coded;
}

// For testing
char* create_some_secret_key() {

  srand (time(NULL));
  int intKey = rand();

  char* ptr_key = static_cast<char*>(static_cast<void*>(&intKey));

  char* key = new char[sizeof(intKey)];

  std::copy(ptr_key, ptr_key + sizeof(intKey), key);

  return key;
}
