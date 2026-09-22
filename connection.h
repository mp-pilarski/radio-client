#ifndef CONNECTION_H
#define CONNECTION_H
#include <cstddef>
#include <string>
#include <sys/types.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// W zadaniu są dwa rodzaje połączeń HTTP i HTTPS - 
// są one obsługiwane przez obiekty HttpConnection i HttpsConnection

// Klasa abstrakcyjna opisująca HttpConnection i HttpsConnection
class Connection {
public:
  virtual ~Connection() = default;
  virtual ssize_t read(char *buffer, size_t n) = 0;
  virtual ssize_t writen(const std::string &data) = 0;
  virtual int get_fd() const = 0;
  virtual bool has_pending_data() = 0;
};

class HttpConnection : public Connection {
private:
    int fd;
public:
     explicit HttpConnection(int socket_fd) : fd(socket_fd) {}
     ~HttpConnection() override;
     ssize_t read(char *buffer, size_t n) override;
     ssize_t writen(const std::string &data) override;
     int get_fd() const override;
     bool has_pending_data() override;
};

class HttpsConnection : public Connection {
private:
    int fd;
    SSL *ssl;
    SSL_CTX *ctx;
public:
     explicit HttpsConnection(int socket_fd, const char *host, SSL_CTX *context);
     ~HttpsConnection() override;
     ssize_t read(char *buffer, size_t n) override;
     ssize_t writen(const std::string &data) override;
     int get_fd() const override;
     bool has_pending_data() override;
};

#endif
