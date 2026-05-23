#include "connection.h"

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

HttpConnection::~HttpConnection() {
    if (fd >= 0) close(fd);
}

ssize_t HttpConnection::read(char* buffer, size_t n) {
    // TODO: zły error handling!
    // return recv(fd, buffer, n, 0);
    ssize_t res;
    do {
        res = recv(fd, buffer, n, 0);
    } while (res < 0 && errno == EINTR);
    return res;
}

// ssize_t HttpConnection::readn(char *buffer, size_t len) {
//       ssize_t res;
//       do {
//           res = recv(fd, buffer, len, 0);
//       } while(res < 0 && errno == EINTR);
//       return res;
// }

bool HttpConnection::writen(const std::string& data) {
    size_t total_sent = 0;
    size_t len = data.length();
    const char* buf = data.c_str();

    while (total_sent < len) {
        ssize_t sent;
        sent = send(fd, buf + total_sent, len - total_sent, 0);

        if (sent <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        total_sent += sent;
    }
    return true;
}

int HttpConnection::get_fd() const { return fd; }

bool HttpConnection::has_pending_data() { return false; }

HttpsConnection::HttpsConnection(int socket_fd, const char* host)
    : fd(socket_fd) {
    // TODO: ctx powinno być singletonem!
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) throw std::runtime_error("Failed to create SSL_CTX");

    // odrzucenie połączenia jeśli weryfikacja certyfikatu kończy się
    // niepowodzeniem
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    // TODO: do usunięcia SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    //  użycie domyślnego zestawu zaufanych certyfikatów
    if (!SSL_CTX_set_default_verify_paths(ctx)) {
        SSL_CTX_free(ctx);
        throw std::runtime_error(
            "Failed to set the default trusted certificate store\n");  // FIXME:
    }
    // MAYBE: ustawienie minimalnej wersji protokołu
    ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        throw std::runtime_error(
            "Failed to create the SSL object\n");  // FIXME: może inny rodzaj
    }

    // połączenie ssl z fd
    SSL_set_fd(ssl, fd);

    if (!SSL_set_tlsext_host_name(ssl, host)) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        throw std::runtime_error(
            "Failed to set the certificate verification hostname\n");  // FIXME:
    }

    if (SSL_connect(ssl) < 1) {
        long verify_err = SSL_get_verify_result(ssl);
        std::string err_msg = "Failed to connect to the server";
        if (verify_err != X509_V_OK) {
            err_msg += std::string(" - SSL verification error: ") + X509_verify_cert_error_string(verify_err);
        }
        SSL_free(ssl);
        SSL_CTX_free(ctx);
    }
}

HttpsConnection::~HttpsConnection() {
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ctx) SSL_CTX_free(ctx);
    if (fd >= 0) close(fd);
}

ssize_t HttpsConnection::read(char* buffer, size_t n) {
    size_t res;
    if (SSL_read_ex(ssl, buffer, n, &res) <= 0) {
        int err = SSL_get_error(ssl, 0);
        // Opcjonalnie: obsługa SSL_ERROR_WANT_READ jeśli używasz non-blocking IO
        if (err == SSL_ERROR_ZERO_RETURN) return 0; // Łagodne zamknięcie połączenia
        return -1; // Błąd wejścia/wyjścia
    }
    return static_cast<ssize_t>(res);
}

// ssize_t HttpsConnection::readn(char *buffer, size_t n) {
//       //TODO: to nie ma sensu!
//       // size_t res;
//       // do {
//       //     res = SSL_read_ex(ssl, buffer, n, &res);
//       // } while(res < 0 && errno == EINTR);
//       // return res;
//       return read(buffer, n);
// }

bool HttpsConnection::writen(const std::string& data) {
    size_t total = 0;
    size_t len = data.length();
    const char* buf = data.c_str();

    while (total < len) {
        size_t n_written = 0;
        if (SSL_write_ex(ssl, buf + total, len - total, &n_written) <= 0) {
            int err = SSL_get_error(ssl, 0);
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                continue;
            }
            return false;
        }
        total += n_written;
    }
    return true;
}

int HttpsConnection::get_fd() const { return fd; }

bool HttpsConnection::has_pending_data() { return SSL_pending(ssl) > 0; }
