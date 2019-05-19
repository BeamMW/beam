// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "errorhandling.h"
#include "buffer.h"
#include <memory>
#include <openssl/err.h>
#include <openssl/dh.h>
#include <openssl/ssl.h>
#include <openssl/conf.h>
#include <openssl/engine.h>

namespace beam { namespace io {

/// App-wide SSL context
class SSLContext {
public:
    using Ptr = std::shared_ptr<SSLContext>;

    static Ptr create_server_ctx(const char* certFileName, const char* privKeyFileName);

    static Ptr create_client_context();

    SSL_CTX* get() { return _ctx; }

    bool is_server() { return _isServer; }

    ~SSLContext();

private:
    SSLContext(SSL_CTX* ctx, bool isServer) : _ctx(ctx), _isServer(isServer) {}

    SSL_CTX* _ctx;
    bool _isServer;
};

class SSLIO {
public:
    /// Decrypted data received from stream. Returns whether to proceed
    using OnDecryptedData = std::function<bool(void* data, size_t size)>;

    /// Encrypted data to be queued to stream
    using OnEncryptedData = std::function<Result(const io::SharedBuffer& data, bool flush)>;

    SSLIO(const SSLIO&) = delete;
    SSLIO& operator=(const SSLIO&) = delete;

    SSLIO(
        const SSLContext::Ptr& ctx,
        const OnDecryptedData& onDecryptedData, const OnEncryptedData& onEncryptedData,
        size_t fragmentSize=16384
    );

    ~SSLIO();

    /// Encrypted data received from stream. Returns whether to proceed
    Result on_encrypted_data_from_stream(const void* data, size_t size);

    void enqueue(const SharedBuffer& buf);

    Result flush();

    void shutdown();

private:
    Result do_handshake();

    Result send_pending_data(bool flush);

    OnDecryptedData _onDecryptedData;
    OnEncryptedData _onEncryptedData;

    size_t _fragmentSize;

    /// Output message
    io::SerializedMsg _outMsg;

    /// ssl ctx
    SSLContext::Ptr _ctx;

    /// connection ctx
    SSL* _ssl = nullptr;
    BIO* _rbio = nullptr;
    BIO* _wbio = nullptr;
};

}} //namespaces
