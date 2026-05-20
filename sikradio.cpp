#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/buffer.h>
#include <openssl/tls1.h>
#include <stdexcept>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <map>
#include <sstream>
#include <poll.h>
#include <memory>
#include <fcntl.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <vector>
#include <algorithm>

#include "common.h"
#include "connection.h"

struct SiteInfo {
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
    bool customPort = false;
};

struct ClientConfig {
    SiteInfo url;
    std::string url_string;
    bool request_metadata = false;
    uint32_t timeout_ms = 5000; //MAYBE: zmienić na inny typ
    int ip_version = AF_UNSPEC;
    int verbosity = 2;
};

enum State {
    AUDIO,
    META_LEN,
    META_BODY
};

struct HTTPResponse {
    int status_code;
    std::string status_msg;
    std::map<std::string, std::string> headers;

    // Metoda do wyciągania wartości nagłówków
    std::string get_header(const std::string& key) const {
        auto it = headers.find(key);
        return it != headers.end() ? it->second : "";
    }
};

void trim(std::string& s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
}

std::string prepareForGetAddr(std::string& s){
    if(s[0] == '[' && s.back() == ']'){
        return s.substr(1, s.length()-2);
    }
    return s;
}

HTTPResponse parseHttpResponse(const std::string& raw_input){
    HTTPResponse response{};
    std::istringstream stream(raw_input);
    std::string line;

    // Pierwsza linia to Wersja i status
    if(std::getline(stream, line)){
        trim(line);
        size_t space1 = line.find(' ');
        size_t space2 = line.find(' ', space1 + 1);

        if (space1 != std::string::npos && space2 != std::string::npos) {
            response.status_code = std::stoi(line.substr(space1 + 1, space2 - space1 - 1));
            response.status_msg = line.substr(space2 + 1);
        }
    }

    // Kolejne linie to wartości pól
    while(std::getline(stream, line)){
        //TODO: przyciac linie
        trim(line);
        if(line.empty()) continue;
        size_t colon_pos = line.find(':');
        if(colon_pos != std::string::npos){
            std::string key = line.substr(0, colon_pos);
            std::string val = line.substr(colon_pos + 1);
            trim(key);
            trim(val);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::tolower(c);});
            response.headers[key] = val;
        }
    }

    return response;
}

SiteInfo parseUrl(std::string& url){
    SiteInfo result;
    //TODO: usprawnić!
    std::cerr << "url: " << url << "\n";
    // Wycięcie fragmentu po # i zignorowanie go
    std::string temp = url;
    auto hash_pos = url.find('#');
    if(hash_pos != std::string::npos) {
        temp = temp.substr(0, hash_pos);
    }

    // MAYBE: czy odcinać queries od path?
    // auto question_pos = temp.find('?');
    // if(question_pos != std::string::npos){
    //     //TODO: rozbić na queries?
    //     temp = temp.substr(0, hash_pos);
    // }

    // Wyciągnięcie scheme
    auto colon_pos = temp.find(':');
    auto slash_pos = temp.find('/');

    if(colon_pos != std::string::npos && (slash_pos == std::string::npos || colon_pos < slash_pos)){
        result.scheme = temp.substr(0, colon_pos);
        temp = temp.substr(colon_pos + 1);
    }

    if(temp.length() >= 2 && temp[0] == '/' && temp[1] == '/'){
        temp = temp.substr(2);

        std::string authority;
        slash_pos = temp.find('/');
        if(slash_pos != std::string::npos) {
            authority = temp.substr(0, slash_pos);
            result.path = temp.substr(slash_pos);
        }else{
            authority = temp;
            result.path = "/";
        }

        if(!authority.empty() && authority[0] == '['){
            auto bracket_end = authority.find(']');
            if(bracket_end != std::string::npos){
                result.host = authority.substr(0, bracket_end+1);
                authority = authority.substr(bracket_end+1);

                if(!authority.empty() && authority[0] == ':'){
                    result.port = authority.substr(1);
                    result.customPort = true;
                }

            }
        }else {
            // Wyciągnięcie portu
            auto port_colon = authority.find(':');
            if(port_colon != std::string::npos) {
                result.host = authority.substr(0, port_colon);
                result.port = authority.substr(port_colon+1);
                result.customPort = true;
            }else{
                result.host = authority;
                result.port = (result.scheme == "https") ? "443" : "80";
            }
        }
    }
    std::cerr << "Protokol: " << result.scheme << "\n";
    std::cerr << "Domena:   " << result.host << "\n";
    std::cerr << "Port:     " << result.port << "\n";
    std::cerr << "Sciezka:  " << result.path << "\n";
    return result;
}

ClientConfig parse_arguments(int argc, char *argv[]){
    ClientConfig config{};
    std::string url;
    int opt;
    bool ipv4_forced = false, ipv6_forced = false;
    while((opt = getopt(argc, argv, "u:mt:46v:q")) != -1){
        switch (opt) {
            case 'u':
                url = optarg;
                break;
            case 'm':
                config.request_metadata = true;
                break;
            case 't':
                config.timeout_ms = read_timeout(optarg);
                break;
            case '4':
                ipv4_forced = true;
                break;
            case '6':
                ipv6_forced = true;
                break;
            case 'v':
                config.verbosity = read_field(optarg);
                break;
            case 'q':
                config.verbosity = 0;
                break;
            default:
                throw std::invalid_argument("Unknown parameter");
        }
    }
    if(ipv4_forced && !ipv6_forced){
        config.ip_version = AF_INET;
    }else if(ipv6_forced && !ipv4_forced){
        config.ip_version = AF_INET6;
    }else{
        config.ip_version = AF_UNSPEC;
    }

    if(url.empty()){
        throw std::invalid_argument("URL can't be empty");
    }
    config.url_string = url;
    config.url = parseUrl(url);
    return config;
}

class RadioClient {
private:
    ClientConfig config;
    std::string stdin_buffer; //FIXME: ??
    std::string meta_buffer = "";
    size_t chars_to_meta;
    size_t meta_int;
    State state = State::AUDIO;

    ssize_t safe_stdout_write(char* buf, size_t n){
        ssize_t written = 0;
        while(written < n) {
            ssize_t res = write(STDOUT_FILENO, buf + written, n - written);
            if(res < 0){
                if(errno == EINTR) continue;
                throw std::runtime_error("Blad zapisu do STDOUT");
            }
            written += res;
        }
        return written;
    }

    //TODO: argument zamienić na wektor
    //TODO: osobny obiekt?
    void parseAudio(char *buf, ssize_t n){
        ssize_t i = 0;
        while(i < n) {
            if(meta_int == 0){
                ssize_t to_write = n-i;
                //write(STDOUT_FILENO, buf + i, to_write);
                i += safe_stdout_write(buf+i, to_write);
            } else {
                if(state == State::AUDIO){
                    size_t to_write = std::min((size_t)n - i, chars_to_meta);
                    //write(STDOUT_FILENO, buf + i, to_write);
                    ssize_t written = safe_stdout_write(buf + i, to_write);
                    i += written;
                    chars_to_meta -= written;
                    if(chars_to_meta == 0) state = State::META_LEN;
                } else if(state == State::META_LEN){
                    unsigned char meta_len = buf[i++];
                    chars_to_meta = meta_len * 16;
                    if(chars_to_meta == 0){
                        state = State::AUDIO;
                        chars_to_meta = meta_int;
                    }else{
                        state = State::META_BODY;
                        meta_buffer.clear();
                    }
                } else {
                    size_t to_write = std::min((size_t)n - i, chars_to_meta);
                    meta_buffer.append(buf + i, to_write);
                    i += to_write;
                    chars_to_meta -= to_write;
                    if(chars_to_meta == 0){
                        std::cerr << "metadata\n";
                        std::cerr.write(meta_buffer.data(), meta_buffer.size());
                        std::cerr.write("\n", 1);
                        state = State::AUDIO;
                        chars_to_meta = meta_int;
                    }
                }
            }
        }
    }

    std::unique_ptr<Connection> serverConnect(SiteInfo url){
        //połaczenie do serwera -> też kolejna funkcja
        addrinfo hints{}, *res, *rp;
        hints.ai_family = config.ip_version;
        hints.ai_socktype = SOCK_STREAM;

        //FIXME: to jest brzydkie!
        std::cerr << prepareForGetAddr(url.host) << "\n";
        int err = getaddrinfo(prepareForGetAddr(url.host).c_str(), url.port.c_str(), &hints, &res);
        if(err != 0){
            std::cerr << "Bład nawiazania polaczenia: " << std::string(gai_strerror(err));
            throw std::runtime_error("Błąd nawiązania połaczenia " + std::string(gai_strerror(err)));
        }
        int fd = -1;
        char ipstr[INET6_ADDRSTRLEN];
        //TODO: czy to powinna być pętla?
        for(rp = res; rp != nullptr; rp = rp->ai_next){
            void *addr;
            char *ipver;
            struct sockaddr_in *ipv4;
            struct sockaddr_in6 *ipv6;
            // get the pointer to the address itself,
            // different fields in IPv4 and IPv6:
            if (rp->ai_family== AF_INET) { // IPv4
                ipv4= (struct sockaddr_in *)rp->ai_addr;
                addr = &(ipv4->sin_addr);
                ipver= "IPv4";
            } else { // IPv6
                ipv6= (struct sockaddr_in6 *)rp->ai_addr;
                addr = &(ipv6->sin6_addr);
                ipver= "IPv6";
            }
            // convert the IP to a string and print it:
            inet_ntop(rp->ai_family, addr, ipstr, sizeof ipstr);
            //printf(" %s: %s\n", ipver, ipstr);

            fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if(fd == -1) continue;

            timeval tv;
            tv.tv_sec = config.timeout_ms / 1000;
            tv.tv_usec = (config.timeout_ms % 1000) * 1000;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            if(connect(fd, rp->ai_addr, rp->ai_addrlen) != -1){
                break;
            }
            close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        std::cerr << "połączono!\n";
        if(fd == -1) throw std::runtime_error("brak serwera");
        //fcntl(fd, F_SETFL, O_NONBLOCK);
        // timeval tv { 0, config.timeout_ms };
        // setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        // setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        return (url.scheme == "https") ?
        std::unique_ptr<Connection>(new HttpsConnection(fd, url.host.c_str())) :
        std::unique_ptr<Connection>(new HttpConnection(fd));
    }

public:
    RadioClient(const ClientConfig& cfg) : config(cfg) {}

    void run(){
        std::string current_url = config.url_string;
        std::string cookie;
        while(true){
            SiteInfo url = parseUrl(current_url);
        auto conn = serverConnect(url);
        std::string request = "GET " + url.path;
        request += " HTTP/1.1\r\n";
        request += "Host: " + url.host;
        if(url.customPort){
            request += ":" + url.port;
        }
        request += "\r\n";
        request += "Connection: Keep-Alive\r\n"; //TODO: czy tak ma być zawsze?
        if(config.request_metadata){
            request += "Icy-MetaData: 1\r\n";
        }
        if(!cookie.empty()) request += "Cookie: " + cookie + "\r\n";

        request += "\r\n";
        std::cerr << "wysyłam:\n" << request << "\n";
        //ssl_write_all(ssl, request);
        conn->writen(request);

        //TODO: inna funkcja? czytanie po jednym znaku nie jest wydajne
        std::string buffer = "";
        //char chunk[4096]; //FIXME: STAŁE
        size_t header_end_pos = std::string::npos;
        std::cerr << "poczatek czytania nagłówków\n";
        bool in_headers = true;
        while (in_headers) {
            char c;
            ssize_t res = conn->read(&c, 1);
            if(res < 0) throw std::runtime_error("Błąd podczas czytania nagłówków");
            buffer += c;
            //std::cerr << c << "\n";

            if(buffer.length() >= 4 && buffer.substr(buffer.length() - 4) == "\r\n\r\n"){
                in_headers = false;
            }
        }
        std::cerr << "koniec czytania nagłówków\n";
        std::string raw_headers = buffer;

        std::cerr << "response:\n";
        std::cerr << raw_headers << "\n";
        HTTPResponse response = parseHttpResponse(raw_headers);

        std::string temp_cookie = response.get_header("set-cookie");
        if (!temp_cookie.empty()) cookie = temp_cookie.substr(0, temp_cookie.find(';'));
        if(response.status_code == 200){
            std::cerr << "OK\n";
        }else if(response.status_code >= 300 && response.status_code < 400){
            std::cerr << "redirect\n";
            std::string loc = response.get_header("location");
            current_url = loc;
            if(current_url == ""){
                throw std::runtime_error("Serwer wysłał redirect ale nie podał nowej lokalizacji");
            }
            continue;
        }else{
            //TODO: obsługa innych odpowiedzi
            std::cerr << "Inny kod: " << response.status_code << "\n";
            throw std::runtime_error("Serwer rzucił nieznany kod błędu");
        }

        // Skonfigurować poll
        // 1. socket -> audio z metadanymi
        // 2. stdin -> przerwanie od użytkownika
        //TODO: zastosować stałe zamiast indeksów oraz wydzielić część kodu do osobnej funkcji
        pollfd fds[2];
        fds[0].fd = conn->get_fd();
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        fds[1].fd = STDIN_FILENO;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        char buf_c[8192]; //FIXME: STAŁE i lepiej vector
        std::string meta_int_key = response.get_header("icy-metaint");
        std::cerr << meta_int_key << "\n";
        meta_int = 0;
        if(meta_int_key != ""){
            try {
                meta_int = std::stoi(meta_int_key);
            } catch (const std::exception& e){
                std::cerr << "bład podczas czytania icy-metaint\n";
            }
        }else{
            std::cerr << "brak metaint\n";
        }
        chars_to_meta = meta_int;
        state = State::AUDIO;
        meta_buffer.clear();

        bool reconnect = false;
        while(!reconnect){
            int pending = conn->has_pending_data();
            int timeout = (pending > 0) ? 0 : config.timeout_ms;
            int p = poll(fds, 2, timeout);

            if(p < 0) {
                if(errno == EINTR) continue;
                throw std::runtime_error("Blad krytyczny poll()");
            }

            if(p == 0 && pending == 0){
                std::cerr << "timeout\n";
                reconnect = true;
                break;
            }

            // Wejście od użytkownika
            if(fds[1].fd != -1 && fds[1].revents & (POLLIN | POLLERR)){
                fds[1].revents = 0;
                char buf[128];
                ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                if(n > 0){
                    stdin_buffer.append(buf, n);
                    if(stdin_buffer.find("quit") != std::string::npos){
                        std::cerr << "Odczytano quit -> zakończono przez użytkownika\n";
                        //return;
                        exit(0);
                    }
                    if(stdin_buffer.length() > 1024) stdin_buffer.clear();
                } else if(n == 0){
                    //zakończenie strumienia STDIN
                    fds[1].fd = -1;
                }//TODO: inne przypadki do error handlingu
            }

            // audio
            if((fds[0].revents & (POLLIN | POLLERR)) || pending > 0){
                fds[0].revents = 0;
                    size_t written;
                    ssize_t n  = conn->read(buf_c, sizeof(buf_c));
                    if(n == 0){
                        exit(0);
                        // std::cerr << "serwer zamknal strumien\n";
                        // reconnect = true;
                        // break;
                    } else if(n < 0){
                        std::cerr << "blad strumienia";
                        reconnect = true;
                        break;
                    } //TODO: inne przypadki do error handlingu
                    std::cerr << "input z socketa: " << n << "\n";
                    parseAudio(buf_c, n);
                    std::cerr << "koniec inputu z socketa\n";
            }
        }
        }
    }

};

int main(int argc, char *argv[]){
    try{
        ClientConfig config = parse_arguments(argc, argv);
        RadioClient client(config);
        client.run();
    } catch(const std::exception &e){
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
