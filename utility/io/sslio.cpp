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

#include "utility/logger.h"

namespace beam { namespace io {

namespace {

SSL_CTX* init_ctx(bool isServer) {
    static const char* cipher_settings = "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH";
    static const char* srtp_settings = "SRTP_AES128_CM_SHA1_80";

    SSL_CTX* ctx = SSL_CTX_new(isServer ? TLS_server_method() : TLS_client_method());
    if (!ctx) {
        throw std::runtime_error("SSL_CTX_new failed");
    }
    if (SSL_CTX_set_cipher_list(ctx, cipher_settings) != 1) {
        throw std::runtime_error("SSL_CTX_set_cipher_list failed");
    }
    // TODO ???
    if (SSL_CTX_set_tlsext_use_srtp(ctx, srtp_settings) != 0) {
        throw std::runtime_error("SSL_CTX_set_tlsext_use_srtp failed");
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
    SSL_CTX_set_verify(
        ctx, SSL_VERIFY_PEER,
        [](int ok, X509_STORE_CTX* ctx) ->int {
            // TODO !!!
            return 1;
        }
    );
    return Ptr(new SSLContext(ctx, false));
}

SSLContext::~SSLContext() {
    SSL_CTX_free(_ctx);
}

SSLIO::SSLIO(
    const SSLContext::Ptr& ctx,
    OnDecryptedData&& onDecryptedData, OnEncryptedData&& onEncryptedData,
    size_t fragmentSize
) :
    _onDecryptedData(onDecryptedData),
    _onEncryptedData(onEncryptedData),
    _ctx(ctx)
{
    _ssl = SSL_new(_ctx->get());
    if (!_ssl) {
        LOG_ERROR() << "SSL_new failed";
        IO_EXCEPTION(EC_SSL_ERROR);
    }

    _inMemoryIO = BIO_new(BIO_s_mem());
    _outMemoryIO = BIO_new(BIO_s_mem());
    if (!_inMemoryIO || !_outMemoryIO) {
        LOG_ERROR() << "BIO_new failed";
        IO_EXCEPTION(EC_SSL_ERROR);
    }

    BIO_set_mem_eof_return(_inMemoryIO, EC_EOF);
    BIO_set_mem_eof_return(_outMemoryIO, EC_EOF);

    SSL_set_bio(_ssl, _inMemoryIO, _outMemoryIO);

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

}

SSLIO::IOState SSLIO::get_iostate(int retCode) {
    int errCode = SSL_get_error(_ssl, retCode);
    if (errCode == 0) return io_ok;
    if (errCode == SSL_ERROR_WANT_READ || errCode == SSL_ERROR_WANT_WRITE) return io_handshaking;
    return io_error;
}

SSLIO::IOState SSLIO::do_handshake() {
    IOState state = get_iostate(SSL_do_handshake(_ssl));
    if (state != io_error) {
        flush_internal();
        if (!flush_write_buffer()) state = io_error;
    }
    return state;
}

void SSLIO::flush_internal() {
    int bytes = BIO_pending(_outMemoryIO);
    if (bytes <= 0) return;
    auto p = alloc_heap((size_t)bytes);
    assert(p.first);
    int bytesRead = BIO_read(_outMemoryIO, p.first, bytes);
    assert(bytesRead == bytes);
    _outMsg.append(p.first, size_t(bytesRead), p.second, false);
}

Result SSLIO::on_encrypted_data_from_stream(const void *data, size_t size) {
    const uint8_t* ptr = (const uint8_t*)data;
    int bytesRemaining = (int)size;

    while (bytesRemaining > 0) {
        int r = BIO_write(_inMemoryIO, ptr, bytesRemaining);
        if (r > 0) {
            bytesRemaining -= r;
            ptr += r;


        } else {
            IOState st = get_iostate(r);
            if (st == io_error)
                return make_result(EC_SSL_ERROR);
            if (st == io_handshaking)
                continue;
        }

        if (!SSL_is_init_finished(_ssl)) {
            IOState st = do_handshake();
            if (st == io_error)
                return make_result(EC_SSL_ERROR);
            if (st == io_handshaking)
                break;
        }


    }


        if (written <= 0) {
            return false;
        }
        ptr += written;
        bytesRemaining -= written;

        if (!SSL_is_init_finished(_ssl)) {


            n = SSL_accept(client.ssl);
            status = get_sslstatus(client.ssl, n);

            /* Did SSL request to write bytes? */
            if (status == SSLSTATUS_WANT_IO)
                do {
                    n = BIO_read(client.wbio, buf, sizeof(buf));
                    if (n > 0)
                        queue_encrypted_bytes(buf, n);
                    else if (!BIO_should_retry(client.wbio))
                        return -1;
                } while (n>0);

            if (status == SSLSTATUS_FAIL)
                return -1;

            if (!SSL_is_init_finished(client.ssl))
                return 0;
        }

        /* The encrypted data is now in the input bio so now we can perform actual
         * read of unencrypted data. */

        do {
            n = SSL_read(client.ssl, buf, sizeof(buf));
            if (n > 0)
                client.io_on_read(buf, (size_t)n);
        } while (n > 0);

        status = get_sslstatus(client.ssl, n);

        /* Did SSL request to write bytes? This can happen if peer has requested SSL
         * renegotiation. */
        if (status == SSLSTATUS_WANT_IO)
            do {
                n = BIO_read(client.wbio, buf, sizeof(buf));
                if (n > 0)
                    queue_encrypted_bytes(buf, n);
                else if (!BIO_should_retry(client.wbio))
                    return -1;
            } while (n>0);

        if (status == SSLSTATUS_FAIL)
            return -1;
}


}} //namespaces
