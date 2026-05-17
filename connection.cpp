#include <cerrno>
#include <cstddef>
#include <openssl/crypto.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "connection.h"


HttpConnection::~HttpConnection() {
    if (fd >= 0)
      close(fd);
}

ssize_t HttpConnection::read(char *buffer, size_t n) {
    return recv(fd, buffer, n, 0);
}

ssize_t HttpConnection::readn(char *buffer, size_t len) {
      ssize_t res;
      do {
          res = recv(fd, buffer, len, 0);
      } while(res < 0 && errno == EINTR);
      return res;
}

bool HttpConnection::writen(const std::string &data) {
    size_t total_sent = 0;
    size_t len = data.length();
    const char *buf = data.c_str();

    while (total_sent < len) {
      ssize_t sent;
      sent = send(fd, buf + total_sent, len - total_sent, 0);

      if (sent <= 0) {
        if (errno == EINTR)
          continue;
        return false;
      }
      total_sent += sent;
    }
    return true;
  }

int HttpConnection::get_fd() const { return fd; }

bool HttpConnection::has_pending_data()  { return false; }

HttpsConnection::HttpsConnection(int socket_fd, const char *host) : fd(socket_fd) {
      //TODO: ctx powinno być singletonem!
      ctx = SSL_CTX_new(TLS_client_method());
      // odrzucenie połączenia jeśli weryfikacja certyfikatu kończy się niepowodzeniem
      SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
      // użycie domyślnego zestawu zaufanych certyfikatów
      if(!SSL_CTX_set_default_verify_paths(ctx)){
          throw std::runtime_error("Failed to set the default trusted certificate store\n"); //FIXME: może inny rodzaj wyjątku + teraz jest wyciek
          exit(1);
      }
      //MAYBE: ustawienie minimalnej wersji protokołu
      ssl = SSL_new(ctx);
      if(ssl == nullptr){
          throw std::runtime_error("Failed to create the SSL object\n"); //FIXME: może inny rodzaj wyjątku + teraz jest wyciek
          exit(1);
      }

      // połączenie ssl z fd
      SSL_set_fd(ssl, fd);

      if(!SSL_set_tlsext_host_name(ssl, host)) {
          throw std::runtime_error("Failed to set the certificate verification hostname\n"); //FIXME: może inny rodzaj wyjątku + teraz jest wyciek
          exit(1);
      }

      if(SSL_connect(ssl) < 1){
          std::cerr << "Failed to connect to the server\n";
          if (SSL_get_verify_result(ssl) != X509_V_OK){
              printf("Verify error: %s\n", X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
              exit(1); //FIXME: problem z wyciekiem
          }
      }
  }

HttpsConnection::~HttpsConnection() {
      if (ssl){
          SSL_shutdown(ssl);
          SSL_free(ssl);
      }
      if(ctx) SSL_CTX_free(ctx);
      if(fd >= 0) close(fd);
}

ssize_t HttpsConnection::read(char *buffer, size_t n){
    size_t res;
    SSL_read_ex(ssl, buffer, n, &res);
    return static_cast<ssize_t>(res);
}

ssize_t HttpsConnection::readn(char *buffer, size_t n) {
      //TODO: to nie ma sensu!
      // size_t res;
      // do {
      //     res = SSL_read_ex(ssl, buffer, n, &res);
      // } while(res < 0 && errno == EINTR);
      // return res;
      return read(buffer, n);
}

bool HttpsConnection::writen(const std::string &data) {
      size_t total = 0;
      size_t len = data.length();
      const char* buf = data.c_str();\

      while(total < len){
          size_t n_written = 0;
          if(!SSL_write_ex(ssl, buf + total, len - total, &n_written)){
              int err = SSL_get_error(ssl, 0);
              if(err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                  continue;
              }
              return -1;
          }
          total += n_written;
      }
      return (int)total;
  }

  int HttpsConnection::get_fd() const { return fd; }

  bool HttpsConnection::has_pending_data() { return SSL_pending(ssl) > 0; }
