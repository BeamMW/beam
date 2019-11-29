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

#include "sslio.h"
#include <openssl/bio.h>
#include <assert.h>

#define LOG_DEBUG_ENABLED 1
#include "utility/logger.h"

namespace beam { namespace io {

namespace {

struct SSLInitializer {
    SSLInitializer() {
        SSL_library_init();
        SSL_load_error_strings();
        ERR_load_BIO_strings();
        OpenSSL_add_all_algorithms();
        ERR_load_crypto_strings();
        ok = true;
    }

    ~SSLInitializer() {
        // TODO
    }

    bool ok=false;
};

SSLInitializer g_sslInitializer;

SSL_CTX* init_ctx(bool isServer) {
    if (!g_sslInitializer.ok) {
        LOG_ERROR() << "SSL init failed";
        IO_EXCEPTION(EC_SSL_ERROR);
    }

    static const char* cipher_settings = "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH";

    SSL_CTX* ctx = SSL_CTX_new(isServer ? SSLv23_server_method() : SSLv23_client_method());
    if (!ctx) {
        LOG_ERROR() << "SSL_CTX_new failed";
        IO_EXCEPTION(EC_SSL_ERROR);
    }
    if (SSL_CTX_set_cipher_list(ctx, cipher_settings) != 1) {
        LOG_ERROR() << "SSL_CTX_set_cipher_list failed";
        IO_EXCEPTION(EC_SSL_ERROR);
    }

    SSL_CTX_set_options(ctx, SSL_OP_ALL|SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
    return ctx;
}

} //namespace

SSLContext::Ptr SSLContext::create_server_ctx(const char* certFileName, const char* privKeyFileName) {
    assert(certFileName && privKeyFileName);

    SSL_CTX* ctx = init_ctx(true);
    if (SSL_CTX_use_certificate_file(ctx, certFileName, SSL_FILETYPE_PEM) != 1) {
        LOG_ERROR() << "SSL_CTX_use_certificate_file failed, " << certFileName;
        IO_EXCEPTION(EC_SSL_ERROR);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, privKeyFileName, SSL_FILETYPE_PEM) != 1) {
        LOG_ERROR() << "SSL_CTX_use_PrivateKey_file failed " << privKeyFileName;
        IO_EXCEPTION(EC_SSL_ERROR);
    }
    if (SSL_CTX_check_private_key(ctx) != 1) {
        LOG_ERROR() << "SSL_CTX_check_private_key failed" << privKeyFileName;
        IO_EXCEPTION(EC_SSL_ERROR);
    }
    return Ptr(new SSLContext(ctx, true));
}

SSLContext::Ptr SSLContext::create_client_context() {
    SSL_CTX* ctx = init_ctx(false);
    /*SSL_CTX_set_verify(
        ctx, SSL_VERIFY_PEER,
        [](int ok, X509_STORE_CTX* ctx) ->int {
            // TODO !!!
            return 1;
        }
    );*/
    return Ptr(new SSLContext(ctx, false));
}

SSLContext::~SSLContext() {
    SSL_CTX_free(_ctx);
}

SSLIO::SSLIO(
    const SSLContext::Ptr& ctx,
    const OnDecryptedData& onDecryptedData, const OnEncryptedData& onEncryptedData,
    size_t fragmentSize
) :
    _onDecryptedData(onDecryptedData),
    _onEncryptedData(onEncryptedData),
    _fragmentSize(fragmentSize > 1000000 ? 1000000 : fragmentSize),
    _ctx(ctx)
{
    _ssl = SSL_new(_ctx->get());
    if (!_ssl) {
        LOG_ERROR() << "SSL_new failed";
        IO_EXCEPTION(EC_SSL_ERROR);
    }

    SSL_set_mode(_ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_set_mode(_ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_set_mode(_ssl, SSL_MODE_AUTO_RETRY);

    _rbio = BIO_new(BIO_s_mem());
    _wbio = BIO_new(BIO_s_mem());
    if (!_rbio || !_wbio) {
        LOG_ERROR() << "BIO_new failed";
        IO_EXCEPTION(EC_SSL_ERROR);
    }

    BIO_set_mem_eof_return(_rbio, EC_EOF);
    BIO_set_mem_eof_return(_wbio, EC_EOF);

    SSL_set_bio(_ssl, _rbio, _wbio);

    if (_ctx->is_server()) {
        SSL_set_accept_state(_ssl);
    } else {
        SSL_set_connect_state(_ssl);
    }
}

SSLIO::~SSLIO() {
    if (_ssl) SSL_free(_ssl);
}

void SSLIO::enqueue(const io::SharedBuffer& buf) {
    if (!buf.empty()) _outMsg.push_back(buf);
}

Result SSLIO::flush() {
    Result res;
    if (!SSL_is_init_finished(_ssl)) {
        res = do_handshake();
    }
    if (res && !_outMsg.empty() && SSL_is_init_finished(_ssl)) {
        auto buf = io::normalize(_outMsg, false);
        _outMsg.clear();
        size_t bytesRemaining = buf.size;
        const uint8_t* ptr = buf.data;
        while (bytesRemaining > 0) {
            int n = (bytesRemaining < _fragmentSize) ? (int)bytesRemaining : (int)_fragmentSize;
            n = SSL_write(_ssl, ptr, n);
            if (n <= 0) return make_unexpected(EC_SSL_ERROR);
            bytesRemaining -= n;
            ptr += n;
            res = send_pending_data(bytesRemaining == 0);
            if (!res) break;
        }
    }
    return res;
}

void SSLIO::shutdown() {
    flush();
    SSL_shutdown(_ssl);
    send_pending_data(true);
}

Result SSLIO::send_pending_data(bool flush) {
    int bytes = BIO_pending(_wbio);
    if (bytes < 0) return make_unexpected(EC_SSL_ERROR);
    if (bytes > 0) {
        //LOG_DEBUG() << __FUNCTION__ << TRACE(this) << TRACE(bytes);
        auto p = alloc_heap((size_t) bytes);
        assert(p.first);
        int bytesRead = BIO_read(_wbio, p.first, bytes);
        assert(bytesRead == bytes);
        return _onEncryptedData(SharedBuffer(p.first, size_t(bytesRead), p.second), flush);
    }
    return Ok();
}

Result SSLIO::do_handshake() {
    if (!SSL_is_init_finished(_ssl)) {
        SSL_do_handshake(_ssl);
        return send_pending_data(true);
    }
    return Ok();
}

Result SSLIO::on_encrypted_data_from_stream(const void *data, size_t size) {
    //LOG_DEBUG() << TRACE(size); // << std::string((const char*)data, size);

    void* buf = alloca(_fragmentSize);
    auto ptr = (const uint8_t *) data;
    int bytesRemaining = (int) size;

    int r = 0;
    Result res;

    while (bytesRemaining > 0) {
        r = BIO_write(_rbio, ptr, bytesRemaining);
        if (r <= 0) return make_unexpected(EC_SSL_ERROR);

        bytesRemaining -= r;
        ptr += r;

        if (!SSL_is_init_finished(_ssl)) {
            res = do_handshake();
            if (!res) return res;
            if (!SSL_is_init_finished(_ssl)) {
                break;
            } else if (!_outMsg.empty()) {
                res = flush();
                if (!res) return res;
            }
        }

        do {
            r = SSL_read(_ssl, buf, (int) _fragmentSize);
            if (r > 0) {
                if (!_onDecryptedData(buf, (size_t) r)) {
                    // at this time, the object may be deleted
                    return make_unexpected(EC_ENOTCONN);
                }
            }
        } while (r > 0);

        r = SSL_get_error(_ssl, r);
        if (r == SSL_ERROR_WANT_WRITE) {
            res = send_pending_data(true);
            if (!res) return res;
        } else if (r == SSL_ERROR_ZERO_RETURN) {
            break; // EOF will be sent on TCP stream
        } else if (r == SSL_ERROR_SYSCALL || r == SSL_ERROR_SSL) {
            return make_unexpected(EC_SSL_ERROR);
        }
    }
    return Ok();
}

}} //namespaces
