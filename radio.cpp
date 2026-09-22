#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <signal.h>

#include "common.h"
#include "connection.h"
#include "cookie.h"

struct ClientConfig {
    SiteInfo url;
    std::string url_string;
    bool request_metadata = false;
    uint32_t timeout_ms = DEFAULT_TIMEOUT;
    uint8_t ip_version = AF_UNSPEC;
    LoggingLevel verbosity = CRITICAL_ERROR;
};

enum State { AUDIO, META_LEN, META_BODY };

ClientConfig config;

void log(LoggingLevel level, const std::string& msg) {
    if (config.verbosity >= level) std::cerr << msg << "\n";
}

void print_current_time() {
    auto now = std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now());
    auto local = std::chrono::zoned_time{std::chrono::current_zone(), now};
    log(COMMUNICATION, std::format("{:%Y.%m.%d %H.%M.%S}", local));
}

ClientConfig parse_arguments(int argc, char* argv[]) {
    ClientConfig cfg{};
    std::string url;
    int opt;
    bool ipv4_forced = false, ipv6_forced = false;
    while ((opt = getopt(argc, argv, "u:mt:46v:q")) != -1) {
        switch (opt) {
            case 'u':
                url = optarg;
                break;
            case 'm':
                cfg.request_metadata = true;
                break;
            case 't':
                cfg.timeout_ms = read_timeout(optarg);
                break;
            case '4':
                ipv4_forced = true;
                break;
            case '6':
                ipv6_forced = true;
                break;
            case 'v':
                cfg.verbosity =
                    static_cast<LoggingLevel>(read_verbosity(optarg));
                break;
            case 'q':
                cfg.verbosity = SILENT;
                break;
            default:
                throw std::invalid_argument("Unknown parameter");
        }
    }
    if (ipv4_forced && !ipv6_forced) {
        cfg.ip_version = AF_INET;
    } else if (ipv6_forced && !ipv4_forced) {
        cfg.ip_version = AF_INET6;
    } else {
        cfg.ip_version = AF_UNSPEC;
    }

    if (url.empty()) {
        throw std::invalid_argument("URL can't be empty");
    }
    cfg.url_string = url;
    cfg.url = parse_url(url);
    return cfg;
}

class RadioClient {
   private:
    ClientConfig config;
    std::string stdin_buffer;
    std::string meta_buffer = "";
    std::vector<char> buffer;
    size_t chars_to_meta;  // Ile znaków zostało do metadanych
    size_t meta_int;       // Co ile znakow pojawiaja sie metadane
    bool metadata = false;
    State state = State::AUDIO;  // Aktualny stan parsera audio
    bool end_program = false;
    SSL_CTX *ssl_ctx = nullptr; // Kontekst OpenSSL
    bool ctx_init = false;

    // Rozdziela metadane od audio
    void parse_audio(const char* buf, ssize_t n) {
        ssize_t i = 0;
        // Nie ma metadanych -> można po prostu wypisać na stdout
        if (!metadata) {
            safe_stdout_write(buf, n);
            return;
        }

        while (i < n) {
            switch (state) {
                // odbieranie AUDIO
                case State::AUDIO: {
                    size_t to_write = std::min((size_t)n - i, chars_to_meta);
                    ssize_t written = safe_stdout_write(buf + i, to_write);
                    i += written;
                    chars_to_meta -= written;
                    if (chars_to_meta == 0) state = State::META_LEN;
                    break;
                }
                // bajt z długością metadanych
                case State::META_LEN: {
                    uint8_t meta_len = buf[i++];
                    chars_to_meta = meta_len * METADATA_MULT;
                    if (chars_to_meta == 0) {
                        state = State::AUDIO;
                        chars_to_meta = meta_int;
                    } else {
                        state = State::META_BODY;
                        meta_buffer.clear();
                    }
                    break;
                }
                // metadane
                case State::META_BODY: {
                    size_t to_write = std::min((size_t)n - i, chars_to_meta);
                    meta_buffer.append(buf + i, to_write);
                    i += to_write;
                    chars_to_meta -= to_write;
                    if (chars_to_meta == 0) {
                        if (config.request_metadata) {
                            // ucinanie \0 z końca metadanych
                            auto null_pos = meta_buffer.find('\0');
                            if (null_pos != std::string::npos) {
                                meta_buffer = meta_buffer.substr(0, null_pos);
                            }
                            if (!meta_buffer.empty()) {
                                std::cerr << meta_buffer << "\n";
                            }
                        }
                        state = State::AUDIO;
                        chars_to_meta = meta_int;
                    }
                    break;
                }
            }
        }
    }

    // Funkcja wykonująca niskopoziomową część łączenia się z serwerem
    std::unique_ptr<Connection> server_connect(SiteInfo& url) {
        // Zgodnie z zaleceniami dokumentacji OpenSSL powinien być dokładnie jeden SSL_CTX na program
        if(url.scheme == "https" && !ctx_init){
            ctx_init = true;
            ssl_ctx = SSL_CTX_new(TLS_client_method());
            if(!ssl_ctx) throw std::runtime_error("Failed to create SSL_CTX");
            // Odrzucenie połączenia jeśli weryfikacja certyfikatu kończy się niepowodzeniem
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
            // Użycie domyślnego zestawu zaufanych certyfikatów
            if (!SSL_CTX_set_default_verify_paths(ssl_ctx)) {
                throw std::runtime_error("Failed to set the default trusted certificate store\n");
            }
        }
        signal(SIGPIPE, SIG_IGN);
        addrinfo hints{}, *res, *rp;
        hints.ai_family = config.ip_version;
        hints.ai_socktype = SOCK_STREAM;

        print_current_time();
        log(COMMUNICATION, "resolving name " + url.host);
        int err = getaddrinfo(prepareForGetAddr(url.host).c_str(), url.port.c_str(), &hints, &res);
        if (err != 0) {
            throw std::runtime_error("getaddrinfo error: " + std::string(gai_strerror(err)));
        }
        int fd = -1;
        char ipstr[INET6_ADDRSTRLEN];
        bool ipv6_brackets = false;
        for (rp = res; rp != nullptr; rp = rp->ai_next) {
            void* addr;
            struct sockaddr_in* ipv4;
            struct sockaddr_in6* ipv6;
            ipv6_brackets = false;
            if (rp->ai_family == AF_INET) {  // IPv4
                ipv4 = (struct sockaddr_in*)rp->ai_addr;
                addr = &(ipv4->sin_addr);
            } else {  // IPv6
                ipv6 = (struct sockaddr_in6*)rp->ai_addr;
                addr = &(ipv6->sin6_addr);
                ipv6_brackets = true;
            }
            // Konwersja adresu IP na tekst
            inet_ntop(rp->ai_family, addr, ipstr, sizeof ipstr);

            fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd == -1) continue;

            if (connect(fd, rp->ai_addr, rp->ai_addrlen) != -1) {
                break;
            }
            close(fd);
            fd = -1;
        }
        freeaddrinfo(res);

        if (fd == -1) throw std::runtime_error("Could not connect to server");
        
        // Przygotowanie komunikatu dla użytkownika
        std::string msg = "connecting to server ";
        if (ipv6_brackets) {
            msg.append("[");
            msg.append(ipstr);
            msg.append("]");
        } else {
            msg.append(ipstr);
        }
        msg.append(":" + url.port);
        log(COMMUNICATION, msg);

        timeval tv;
        tv.tv_sec = config.timeout_ms / 1000;
        tv.tv_usec = (config.timeout_ms % 1000) * 1000;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            close(fd);
            throw std::system_error(errno, std::generic_category(), "setsockopt");
        }
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
            close(fd);
            throw std::system_error(errno, std::generic_category(), "setsockopt");
        }
        // Wlascicielem fd jest obiekt Connection
        if (url.scheme == "https") {
            return std::make_unique<HttpsConnection>(fd, url.host.c_str(), ssl_ctx);
        } else {
            return std::make_unique<HttpConnection>(fd);
        }
    }

    // Funkcja wykonujące wysokopoziomową część łączenia się z serwerem;
    std::unique_ptr<Connection> connection_start() {
        std::string current_url = config.url_string;
        bool redirect = true;
        std::unique_ptr<Connection> conn = nullptr;
        CookieManager cookie_mgmt = {};
        
        while (redirect) {
            redirect = false;
            SiteInfo url = parse_url(current_url);
            conn = server_connect(url);

            // Przygotowanie requestu
            std::string request = "GET " + url.path;
            request += " HTTP/1.1\r\n";
            request += "Host: " + url.host + "\r\n";
            // Port ma nie byc uwzgledniany w nagłowku Host
            request += "Connection: Keep-Alive\r\n";
            if (config.request_metadata) {
                request += "Icy-MetaData: 1\r\n";
            }
            // Dodanie zapisanych cookies do requestu
            std::string cookies = cookie_mgmt.get_cookie_header(
                url.host, url.path, url.scheme == "https");
            if (!cookies.empty()) {
                request += "Cookie: ";
                request += cookies;
                request += "\r\n";
            }
            request += "\r\n";

            log(LoggingLevel::COMMUNICATION, request);
            
            if(conn->writen(request) < static_cast<ssize_t>(request.length())){
                throw std::runtime_error("Failed write");
            }

            // Odbiór nagłówków od serwera:
            // Dane sa odbierane w potencjalnie duzych porcjach,
            // dlatego jest możliwe, że w tej części będzie odebrany początek audio
            buffer.resize(BIG_BUF);
            std::string headers = "";
            std::string leftover_audio = "";
            bool in_headers = true;
            bool checked_start = false;
            while (in_headers) {
                ssize_t res = conn->read(buffer.data(), buffer.size());
                if (res < 0) {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK){
                        log(NONCRITICAL_ERROR, "Noncritical socket error: " + std::string(strerror(errno)));
                        continue;
                    }
                    throw std::system_error(errno, std::generic_category(), "header read");
                }
                // Serwer zakończył połączenie -> zwracamy zero
                if (res == 0) {
                    end_program = true;
                    return nullptr;
                }

                headers.append(buffer.data(), res);

                // Sprawdzenie początku odpowiedzi - wstępne odrzucenie nieprawidłowych odpowiedzi serwera
                if (!checked_start && headers.length() >= 4) {
                    checked_start = true;
                    if (!(headers.substr(0, 4) == "HTTP" ||
                          headers.substr(0, 3) == "ICY")) {
                        throw std::runtime_error(
                            "Server did not send proper response");
                    }
                }

                // Nagłówki kończą się jeśli wystąpił podwójny \r\n
                if (headers.length() >= 4){
                    auto end_pos = headers.find("\r\n\r\n");
                    if(end_pos != std::string::npos){
                        in_headers = false;
                        leftover_audio = headers.substr(end_pos + 4);
                        headers.resize(end_pos+4);
                    }
                }
            }

            log(LoggingLevel::COMMUNICATION, headers);
            HTTPResponse response = parse_http_response(headers);

            cookie_mgmt.update_from_response(response, url.host);
            if (response.status_code == 200) {
                log(DIAGNOSTIC, "OK STATUS CODE");
            } else if (response.status_code >= 300 && response.status_code < 400) {
                std::string loc =
                    response.get_header("location").front();
                if (loc == "") {
                    throw std::runtime_error(
                        "Server sent redirect response without Location "
                        "header");
                }
                current_url = resolve_redirect(loc, url);
                redirect = true;
                continue;
            } else {
                throw std::runtime_error("Server send unknown response code: " +
                                         std::to_string(response.status_code));
            }

            // Zapisanie odstępu między metadanymi
            std::string meta_int_key = response.get_header("icy-metaint").front();
            meta_int = 0;
            if (meta_int_key != "") {
                try {
                    meta_int = std::stoi(meta_int_key);
                    metadata = true;
                } catch (const std::exception& e) {
                    log(DIAGNOSTIC, "Error during reading icy-metaint - assuming no metadata");
                    metadata = false;
                }
            } else {
                log(DIAGNOSTIC, "No metaint");
            }
            chars_to_meta = meta_int;
            parse_audio(leftover_audio.c_str(), leftover_audio.length());
        }
        return conn;
    }

   public:
    RadioClient(const ClientConfig& cfg) : config(cfg) {}

    ~RadioClient() {
        // Podczas niszczenia obiektu należy zniszczyć kontekst.
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    }

    void run() {
        state = State::AUDIO;
        while (true) {
            auto conn = connection_start();
            if (end_program) return;

            pollfd fds[2];
            fds[SERVER_FD].fd = conn->get_fd();
            fds[SERVER_FD].events = POLLIN;
            fds[SERVER_FD].revents = 0;

            fds[STDIN_FD].fd = STDIN_FILENO;
            fds[STDIN_FD].events = POLLIN;
            fds[STDIN_FD].revents = 0;

            bool reconnect = false;
            end_program = false;
            auto last_network_activity = std::chrono::steady_clock::now();
            while (!reconnect) {
                bool pending = conn->has_pending_data();
                auto now = std::chrono::steady_clock::now();
                auto elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_network_activity).count();
                auto remaining_timeout = config.timeout_ms - elapsed_ms;

                // Czas minął oraz OpenSSL nie ma zbuforowanych danych
                if (remaining_timeout <= 0 && pending == 0) {
                    log(COMMUNICATION, "data receiving timeout");
                    reconnect = true;
                    break;
                }

                int timeout = pending ? 0 : static_cast<int>(remaining_timeout);
                int p = poll(fds, 2, timeout);

                if (p < 0) {
                    if (errno == EINTR) continue;
                    throw std::system_error(errno, std::generic_category(),
                                            "poll");
                }

                now = std::chrono::steady_clock::now();
                elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_network_activity).count();
                remaining_timeout = config.timeout_ms - elapsed_ms;
                // poll przekroczyl limit czasu
                if (p == 0 && pending == 0 && remaining_timeout < 0) {
                    log(COMMUNICATION, "data receiving timeout");
                    reconnect = true;
                    break;
                }

                // Wejście od użytkownika
                if (fds[STDIN_FD].fd != -1 && fds[1].revents != 0) {
                    if (fds[STDIN_FD].revents & (POLLERR | POLLNVAL)) {
                        fds[STDIN_FD].fd = -1;
                    } else if (fds[STDIN_FD].revents & (POLLIN | POLLHUP)) {
                        char buf[128];
                        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                        if (n > 0) {
                            stdin_buffer.append(buf, n);
                            if (stdin_buffer.find("quit\n") !=
                                std::string::npos) {
                                log(DIAGNOSTIC, "quit on stdin - program terminated by user");
                                return;
                            }
                            if (stdin_buffer.length() > 128)
                                stdin_buffer.clear();
                        } else if (n == 0) {
                            // zakończenie strumienia STDIN
                            log(DIAGNOSTIC, "stdin closed - EOF");
                            fds[STDIN_FD].fd = -1;
                        } else {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                log(NONCRITICAL_ERROR, std::string("stdin error: ") + strerror(errno));
                                fds[STDIN_FD].fd = -1;  // Błąd STDIN, kontynuujemy streaming
                            }
                        }
                    }
                    fds[STDIN_FD].revents = 0;
                }

                // audio
                if (fds[SERVER_FD].fd != -1 && (fds[SERVER_FD].revents != 0 || pending > 0)) {
                    if (fds[SERVER_FD].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        log(NONCRITICAL_ERROR, "Server socket error");
                        reconnect = true;
                        break;
                    }

                    if ((fds[SERVER_FD].revents & POLLIN) || pending > 0) {
                        ssize_t n = conn->read(buffer.data(), buffer.size());
                        if (n == 0) {
                            log(CRITICAL_ERROR, "Server closed connection");
                            return;
                        } else if (n < 0) {
                            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK){
                                log(NONCRITICAL_ERROR, "Noncritical socket error: " + std::string(strerror(errno)));
                                continue;
                            }    
                            throw std::runtime_error("Socket error: " + std::string(strerror(errno)));
                        } else {
                            parse_audio(buffer.data(), n);
                            last_network_activity = std::chrono::steady_clock::now();
                        }
                    }
                    fds[SERVER_FD].revents = 0;
                }
            }
        }
    }
};

int main(int argc, char* argv[]) {
    try {
        config = parse_arguments(argc, argv);
        RadioClient client(config);
        client.run();
    } catch (const std::exception& e) {
        log(CRITICAL_ERROR, e.what());
        return 1;
    }
    return 0;
}
