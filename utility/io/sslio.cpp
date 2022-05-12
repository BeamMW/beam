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
#include <sstream>

#define LOG_DEBUG_ENABLED 1
#include "utility/logger.h"

namespace beam { namespace io {

namespace {

int verify_server(int preverify_ok, X509_STORE_CTX* x509_ctx)
{
    if (!preverify_ok) {
        int error = X509_STORE_CTX_get_error(x509_ctx);
        LOG_ERROR() << "server verification error: " << error << " " << X509_verify_cert_error_string(error);
    }
    return preverify_ok;
}

int verify_client(int preverify_ok, X509_STORE_CTX* x509_ctx)
{
    if (!preverify_ok) {
        int error = X509_STORE_CTX_get_error(x509_ctx);
        LOG_ERROR() << "client verification error: " << error << " " << X509_verify_cert_error_string(error);
    }
    return preverify_ok;
}

void ssl_info(const SSL* ssl, int where, int ret)
{
    const char* str;
    int w;

    w = where & ~SSL_ST_MASK;

    if (w & SSL_ST_CONNECT) str = "SSL_connect";
    else if (w & SSL_ST_ACCEPT) str = "SSL_accept";
    else str = "undefined";
    std::stringstream ss;
    if (where & SSL_CB_LOOP)
    {
        ss <<  str<< ":" <<  SSL_state_string_long(ssl);
    }
    else if (where & SSL_CB_ALERT)
    {
        str = (where & SSL_CB_READ) ? "read" : "write";
        ss << "SSL3 alert " <<
            str << ":" <<
            SSL_alert_type_string_long(ret) << ":" <<
            SSL_alert_desc_string_long(ret);
    }
    else if (where & SSL_CB_EXIT)
    {
        if (ret == 0)
            ss << str << ":failed in "<< SSL_state_string_long(ssl);
        else if (ret < 0)
        {
            ss << str << ":error in " << SSL_state_string_long(ssl)
                << ":" <<
                SSL_alert_type_string_long(ret) << ":" <<
                SSL_alert_desc_string_long(ret);
        }
    }

    LOG_DEBUG() << TRACE(ssl) << "  " << ss.str();
}

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

    //SSL_CTX_set_info_callback(ctx, ssl_info);

    return ctx;
}

bool load_system_certificate_authority(SSL_CTX* ctx)
{
#ifdef WIN32
    PCCERT_CONTEXT pContext = nullptr;
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);

    HCERTSTORE hStore = CertOpenSystemStoreW(NULL, L"CA");
    if (!hStore)
        return false;

    while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != nullptr) {
        X509* x509 = d2i_X509(nullptr, (const unsigned char**)&pContext->pbCertEncoded, pContext->cbCertEncoded);
        if (x509) {
            int i = X509_STORE_add_cert(store, x509);
            if (i == 0) {
                LOG_ERROR() << "Failed to add certificate from system store";
            }
            else if (i == 1) {
                LOG_VERBOSE() << "Loaded certificate from system store";
            }
            X509_free(x509);
        }
    }

    CertCloseStore(hStore, 0);
#endif //WIN32
    return true;
}

void setup_certificate(SSL_CTX* ctx, const char* certFileName, const char* privKeyFileName)
{
    if (certFileName && privKeyFileName)
    {
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
    }
}

void setup_verification_paths(SSL_CTX* ctx)
{
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        LOG_ERROR() << "SSL_CTX_set_default_verify_paths failed";
        IO_EXCEPTION(EC_SSL_ERROR);
    }
}

} //namespace

SSLContext::Ptr SSLContext::create_server_ctx(const char* certFileName, const char* privKeyFileName,
                                              bool requestCertificate, bool rejectUnauthorized) {
    assert(certFileName && privKeyFileName);
    
    SSL_CTX* ctx = init_ctx(true);
    setup_certificate(ctx, certFileName, privKeyFileName);

    // the server will not send a client certificate request to the client, so the client will not send a certificate.
    int verificationMode = SSL_VERIFY_NONE;
    if (requestCertificate)
    {
        // the server sends a client certificate request to the client. The certificate returned (if any) is checked. 
        // If the verification process fails, the TLS/SSL handshake is immediately terminated with an alert message containing the reason for the verification failure.
        verificationMode = SSL_VERIFY_PEER;
        if (rejectUnauthorized)
        {
            verificationMode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        }
    }
    SSL_CTX_set_verify(ctx, verificationMode, verify_server);

    setup_verification_paths(ctx);

    return Ptr(new SSLContext(ctx, true));
}

SSLContext::Ptr SSLContext::create_client_context(const char* certFileName, const char* privKeyFileName, bool rejectUnauthorized) {
    SSL_CTX* ctx = init_ctx(false);
    load_system_certificate_authority(ctx);
    setup_certificate(ctx, certFileName, privKeyFileName);

    int verifyMode = (rejectUnauthorized == false) ?
        // if not using an anonymous cipher (by default disabled), the server will send a certificate which will be checked.
        // The result of the certificate verification process can be checked after the TLS/SSL handshake using the SSL_get_verify_result(3) function. 
        // The handshake will be continued regardless of the verification result.
        SSL_VERIFY_NONE
        :
        // the server certificate is verified.
        // If the verification process fails, the TLS / SSL handshake is immediately terminated with an alert message containing the reason for the verification failure.
        // If no server certificate is sent, because an anonymous cipher is used, SSL_VERIFY_PEER is ignored.
        SSL_VERIFY_PEER;    
    
    SSL_CTX_set_verify(ctx, verifyMode, rejectUnauthorized ? verify_client : nullptr);
    
    setup_verification_paths(ctx);

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
