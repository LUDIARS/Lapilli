#include "lapilli/http/tls.hpp"

#include <boost/asio/ip/address.hpp>

#include <openssl/ssl.h>
#include <openssl/x509.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace lapilli::http {

int configure_client_tls(ssl_ctx_st* context) {
    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_default_verify_paths(context);
    int added = 0;
#ifdef _WIN32
    // OpenSSL has no Windows trust store: import the system ROOT store.
    if (HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT")) {
        X509_STORE* x509_store = SSL_CTX_get_cert_store(context);
        PCCERT_CONTEXT cert = nullptr;
        while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr) {
            const unsigned char* data = cert->pbCertEncoded;
            if (X509* x509 = d2i_X509(nullptr, &data, static_cast<long>(cert->cbCertEncoded))) {
                if (X509_STORE_add_cert(x509_store, x509) == 1) ++added;
                X509_free(x509);
            }
        }
        CertCloseStore(store, 0);
    }
#endif
    return added;
}

bool is_ip_literal(const std::string& host) {
    boost::system::error_code error;
    (void)boost::asio::ip::make_address(host, error);
    return !error;
}

bool set_server_name(ssl_st* ssl, const std::string& host) {
    if (host.empty() || is_ip_literal(host)) return true;
    return SSL_set_tlsext_host_name(ssl, host.c_str()) == 1;
}

} // namespace lapilli::http
