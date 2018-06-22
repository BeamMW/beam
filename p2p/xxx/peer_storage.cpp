#include "peer_storage.h"
#include <errno.h>

namespace beam {

using namespace io;

static constexpr size_t ITEM_SIZE = sizeof(PeerInfo);

PeerStorage::~PeerStorage() {
    close();
}

Result PeerStorage::open(const std::string& fileName) {
    if (_file) close();
    _file = fopen(fileName.c_str(), "a+b");
    ErrorCode errorCode = EC_OK;
    if (!_file) {
#ifndef _WIN32
        errorCode = ErrorCode(-errno);
#else
        errorCode = EC_EACCES; //TODO GetLastError and map into io::Error or whatever
#endif
    }
    return make_result(errorCode);
}

static bool seek_end(FILE* f, long& offset) {
    if (fseek(f, 0, SEEK_END) != 0) return false;
    offset = ftell(f);
    return true;
}

static bool seek_to(FILE* f, long offset) {
    return fseek(f, offset, SEEK_SET) == 0;
}

Result PeerStorage::load_peers(const LoadCallback& cb) {
    ErrorCode ec = EC_EBADF;
    if (!_file) {
        return make_unexpected(ec);
    }

    long length=0;
    if (!(seek_end(_file, length) && seek_to(_file, 0))) {
        return make_unexpected(ec);
    }

    if (length % ITEM_SIZE != 0) {
        return make_unexpected(EC_FILE_CORRUPTED);
    }

    size_t nPeers = length / ITEM_SIZE;

    PeerInfo peer;
    for (size_t i=0; i<nPeers; ++i) {
        if (ITEM_SIZE != fread(&peer, 1, ITEM_SIZE, _file)) {
            return make_unexpected(EC_EOF);
        }
        _index[peer.sessionId] = ITEM_SIZE * i;
        cb(peer);
    }

    return Ok();
}

Result PeerStorage::forget_old_peers(uint32_t howLong) {
    // TODO compact db
    return Ok();
}

Result PeerStorage::update_peer(const PeerInfo& peer) {
    ErrorCode ec = EC_EBADF;
    if (!_file) {
        return make_unexpected(ec);
    }

    auto it = _index.find(peer.sessionId);
    long offset=0;
    if (it == _index.end()) {
        if (!seek_end(_file, offset)) {
            return make_unexpected(ec);
        }
        _index[peer.sessionId] = offset;
    } else {
        offset = _index[peer.sessionId];
        if (!seek_to(_file, offset)) {
            return make_unexpected(ec);
        }
    }

    if (ITEM_SIZE != fwrite(&peer, 1, ITEM_SIZE, _file)) {
        _index.erase(peer.sessionId);
        return make_unexpected(ec);
    }

    return Ok();
}

void PeerStorage::close() {
    if (_file) {
        fclose(_file);
        _file = 0;
        _index.clear();
    }
}

} //namespace

