#include <cerrno>
#include <cstddef>
#include <cstdint>
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

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <vector>

#include "common.h"
#include "connection.h"

struct SiteInfo {
    std::string scheme; //HTTP lub HTTPS (może powinno to być coś innego?)
    std::string host;
    std::string port;
    std::string path;
    std::vector<std::pair<std::string, std::string>> queries;
};

struct ClientConfig {
    SiteInfo url;
    std::string url_string;
    bool request_metadata = false;
    uint16_t timeout_ms = 5000; //MAYBE: zmienić na inny typ
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
            response.headers[key] = val;
        }
    }

    return response;
}

SiteInfo parseUrl(std::string& url){
    // //Parsowanie URL -> później wydzielić do common
    // // FIXME: obsługa błędów? co z kropkowanymi adresami np. 123.13.3.11
    // std::string protocol, domain, port, path;
    // size_t pos = url.find("://");
    // protocol = url.substr(0, pos);
    // size_t start = pos + 3;

    // size_t pathPos = url.find('/', start);
    // std::string hostPort;
    // if(pathPos == std::string::npos){
    //     hostPort = url.substr(start);
    //     path = "/";
    // }else{
    //     hostPort = url.substr(start, pathPos - start);
    //     path = url.substr(pathPos);
    // }
    // size_t portPos = hostPort.find(":");
    // if(portPos == std::string::npos){
    //     domain = hostPort;
    //     port = (protocol == "https") ? "443" : "80";
    // }else{
    //     domain = hostPort.substr(0, portPos);
    //     port = hostPort.substr(portPos + 1);
    // }

    // size_t argsStart = path.find("?");
    // if(argsStart != std::string::npos){
    //     std::string args = path.substr(argsStart);

    // }

    // std::cout << "Protokol: " << protocol << "\n";
    // std::cout << "Domena:   " << domain << "\n";
    // std::cout << "Port:     " << port << "\n";
    // std::cout << "Sciezka:  " << path << "\n";
    // return SiteInfo {protocol, domain, port, path};
    SiteInfo result;

    std::string temp = url;
    auto hash_pos = url.find('#');
    if(hash_pos != std::string::npos) {
        // wyciąganie fragmentu i zignorowanie go
        temp = temp.substr(0, hash_pos);
    }

    auto question_pos = temp.find('?');
    if(question_pos != std::string::npos){
        //TODO: rozbić na queries?
        temp = temp.substr(0, hash_pos);
    }

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

        //MAYBE: co z authority jako adresem IPv6 lub IPv4

        auto port_colon = temp.find(':');
        if(port_colon != std::string::npos) {
            result.host = temp.substr(0, port_colon);
            result.port = temp.substr(port_colon+1);
        }else{
            result.host = authority;
            result.port = (result.scheme == "https") ? "443" : "80";
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

    std::unique_ptr<Connection> serverConnect(){
        //połaczenie do serwera -> też kolejna funkcja
        addrinfo hints{}, *res, *rp;
        hints.ai_family = config.ip_version;
        hints.ai_socktype = SOCK_STREAM;

        //FIXME: to jest brzydkie!
        int err = getaddrinfo(config.url.host.c_str(), config.url.port.c_str(), &hints, &res);
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
            printf(" %s: %s\n", ipver, ipstr);

            fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if(fd == -1) continue;

            if(connect(fd, rp->ai_addr, rp->ai_addrlen) != -1){
                break;
            }
            close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        std::cerr << "połączono!\n";
        //TODO: utworzenie odpowiedniego connection!
        return (config.url.scheme == "https") ?
        std::unique_ptr<Connection>(new HttpsConnection(fd, config.url.host.c_str())) :
        std::unique_ptr<Connection>(new HttpConnection(fd));
    }

public:
    RadioClient(const ClientConfig& cfg) : config(cfg) {}

    void run(){
        std::string current_url = config.url_string;
        std::string cookie;
        while(true){
            SiteInfo url = parseUrl(current_url);
        auto conn = serverConnect();
        std::string request = "GET " + url.path + " HTTP/1.1\r\n";
        request += "Host: " + url.host + "\r\n";
        request += "Connection: Keep-Alive\r\n"; //TODO: czy tak ma być zawsze?
        if(config.request_metadata){
            request += "Icy-MetaData: 1\r\n";
        }
        if(!cookie.empty()) request += "Cookie: " + cookie + "\r\n";

        request += "\r\n";
        std::cerr << "wysyłam:\n" << request << "\n";
        //ssl_write_all(ssl, request);
        conn->writen(request);

        //TODO: inna funkcja?
        std::string buffer;
        char chunk[4096]; //FIXME: STAŁE
        size_t header_end_pos = std::string::npos;
        while (true) {
            //ssize_t bytes_read = recv(fd, chunk, sizeof(chunk), 0);
            size_t bytes_read = conn->read(chunk, sizeof(chunk));
            std::cerr << bytes_read << "\n";
            if (bytes_read <= 0) {
                throw std::runtime_error("Błąd połączenia podczas czytania nagłówków");
            }

            buffer.append(chunk, bytes_read);

            // Szukamy podwójnego znaku nowej linii (koniec nagłówków)
            header_end_pos = buffer.find("\r\n\r\n");
            if (header_end_pos != std::string::npos) {
                break;
            }
        }
        // Dzielimy bufor na dwie części:
        // 1. Nagłówki (od początku do końca \r\n\r\n)
        std::string raw_headers = buffer.substr(0, header_end_pos);
        // 2. Reszta (początek muzyki), którą musimy oddać do głównej pętli (?)
        std::string leftover_audio = buffer.substr(header_end_pos + 4);
        //TODO: koniec innej funkcji

        std::cerr << "response:\n";
        std::cerr << raw_headers << "\n";
        HTTPResponse response = parseHttpResponse(raw_headers);

        std::string temp_cookie = response.get_header("Set-Cookie");
        if (!temp_cookie.empty()) cookie = temp_cookie.substr(0, temp_cookie.find(';'));
        if(response.status_code == 200){
            std::cerr << "OK\n";
            //TODO: Kwestia plików cookie
        }else if(response.status_code >= 300 && response.status_code < 400){
            std::cerr << "redirect\n";
            std::string loc = response.get_header("Location");
            current_url = loc;
            continue;
        }else{
            //TODO: obsługa innych odpowiedzi
            std::cerr << "Inny kod: " << response.status_code << "\n";
            exit(0);
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

        char buf[8192]; //FIXME: STAŁE i lepiej vector
        std::string meta_int_key = response.get_header("icy-metaint");
        std::cerr << meta_int_key << "\n";
        size_t meta_int = 0;
        if(meta_int_key != ""){
            meta_int = std::stoi(meta_int_key);
        }else{
            std::cerr << "brak metaint\n";
        }

        State state = State::AUDIO;
        size_t counter_to_meta = meta_int;
        std::string meta_buffer;
        bool reconnect = false;
        while(!reconnect){
            int pending = conn->has_pending_data();
            int timeout = (pending > 0) ? 0 : timeout;
            int p = poll(fds, 2, config.timeout_ms);

            //TODO: obsługa timeoutu
            if(p < 0 && errno == EINTR){
                std::cerr << "EINTR\n";
                continue;
            }
            if(p < 0){
                throw std::runtime_error("Bład poll");
            }
            if(p == 0 && !conn->has_pending_data()){
                std::cerr << "timeout\n";
                break;
            }

            // Wejście od użytkownika
            if(fds[1].revents & (POLLIN | POLLERR)){
                std::cerr << "input z stdin\n";
                fds[1].revents = 0;
                char ch;
                //TODO: tutaj źle
                while (read(STDIN_FILENO, &ch, 1) > 0) {
                    stdin_buffer += ch;
                    if (stdin_buffer.find("quit\n") != std::string::npos) {
                        std::cerr << "Zakończono przez użytkwnika quit";
                        exit(0);
                    }
                }
                std::cerr << "koniec z stdin\n";
            }

            // audio
            if((fds[0].revents & (POLLIN | POLLERR)) || pending > 0){
                fds[0].revents = 0;
                //ssize_t bytes_read = safe_read(buf, 8192);
                //std::cerr << bytes_read << "\n";
                //
                    size_t written;
                    int n  = conn->readn(buf, sizeof(buf));
                    //write(STDOUT_FILENO, buf, n);
                    std::cerr << "input z socketa: " << n << "\n";
                    size_t i = 0;
                    while(i < n){
                        if(meta_int == 0) {
                            ssize_t to_write = n - i;
                            write(STDOUT_FILENO, buf+i, to_write);
                            i += to_write;
                        } else {
                            if(state == State::AUDIO) {
                                size_t to_write = std::min((size_t)n - i, counter_to_meta);
                                write(STDOUT_FILENO, buf + i, to_write);
                                i += to_write;
                                counter_to_meta -= to_write;
                                if(counter_to_meta == 0) state = State::META_LEN;
                            }
                            else if(state == State::META_LEN) {
                                char meta_len = buf[i++];
                                counter_to_meta = meta_len * 16;
                                if(counter_to_meta == 0){
                                    state = State::AUDIO;
                                    counter_to_meta = meta_int;
                                }else{
                                    state = State::META_BODY;
                                    meta_buffer.clear();
                                }
                            }else{
                                size_t to_write = std::min((size_t)n - i, counter_to_meta);
                                meta_buffer.append(buf + i, to_write);
                                i += to_write;
                                counter_to_meta -= to_write;
                                if(counter_to_meta == 0){
                                    std::cerr << "metadata\n";
                                    std::cerr.write(meta_buffer.data(), meta_buffer.size());
                                    std::cerr.write("\n", 1);
                                    state = State::AUDIO;
                                    counter_to_meta = meta_int;
                                }
                            }
                        }
                }
                    std::cerr << "koniec inputu z socketa\n";
            }
        }
        }
    }

};

int main(int argc, char *argv[]){
    ClientConfig config = parse_arguments(argc, argv); //FIXME: obsługa wyjątków
    if(config.request_metadata){
        std::cout << "requested metadata\n";
    }else{
        std::cout << "NOT requested metadata\n";
    }
    std::cout << "config.timeout " << config.timeout_ms << "\n";
    std::cout << "config.verbosity " << config.verbosity << "\n";
    if(config.ip_version == AF_UNSPEC){
        std:: cout << "unspecified ip version\n";
    }else if(config.ip_version == AF_INET){
        std:: cout << "ipv4\n";
    }else{
        std::cout << "ipv6\n";
    }
    RadioClient client(config);
    client.run();
    return 0;
}
