#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/_types/_ssize_t.h>
#include <unistd.h>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>

#include "common.h"

struct SiteInfo {
    std::string type; //HTTP lub HTTPS (może powinno to być coś innego?)
    std::string host;
    std::string port;
    std::string path;
};

struct ClientConfig {
    SiteInfo url;
    bool request_metadata = false;
    uint16_t timeout_ms = 5000; //MAYBE: zmienić na inny typ
    int ip_version = AF_UNSPEC;
    int verbosity = 2;
};

SiteInfo parseUrl(std::string& url){
    //Parsowanie URL -> później wydzielić do common
    // FIXME: obsługa błędów? co z kropkowanymi adresami np. 123.13.3.11
    std::string protocol, domain, port, path;
    size_t pos = url.find("://");
    protocol = url.substr(0, pos);
    size_t start = pos + 3;

    size_t pathPos = url.find('/', start);
    std::string hostPort;
    if(pathPos == std::string::npos){
        hostPort = url.substr(start);
        path = "/";
    }else{
        hostPort = url.substr(start, pathPos - start);
        path = url.substr(pathPos);
    }
    size_t portPos = hostPort.find(":");
    if(portPos == std::string::npos){
        domain = hostPort;
        port = (protocol == "https") ? "443" : "80";
    }else{
        domain = hostPort.substr(0, portPos);
        port = hostPort.substr(portPos + 1);
    }
    std::cout << "Protokol: " << protocol << "\n";
    std::cout << "Domena:   " << domain << "\n";
    std::cout << "Port:     " << port << "\n";
    std::cout << "Sciezka:  " << path << "\n";
    return SiteInfo {protocol, domain, port, path};
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
    config.url = parseUrl(url);
    return config;
}

class RadioClient {
private:
    ClientConfig config;
    int fd = -1;

    //TODO: wyeksportować do common? porównać z kodem z labów
    bool safe_send(const std::string& data){
        size_t total_sent = 0;
        size_t len = data.length();
        const char* buf = data.c_str();

        while(total_sent < len){
            ssize_t sent;
            //TODO: obsługa https
            sent = send(fd, buf + total_sent, len - total_sent, 0);

            if(sent <= 0){
                if(errno == EINTR) continue;
                return false;
            }
            total_sent += sent;
        }
        return true;
    }

    //TODO: wyeksportować do common? porównać z kodem z labów
    ssize_t robust_read(char* buf, size_t len) {
        ssize_t res;
        do {
            //if (conn.is_https) res = SSL_read(conn.ssl, buf, len);
            res = recv(fd, buf, len, 0);
        } while (res < 0 && errno == EINTR);
        return res;
    }

    //TODO: sens czytania tylko jednej linii?
    std::string read_http_line() {
        std::string line;
        char c;
        while (robust_read(&c, 1) == 1) {
            line += c;
            if (line.size() >= 2 && line.substr(line.size() - 2) == "\r\n") {
                return line.substr(0, line.size() - 2);
            }
        }
        return line;
    }

    void serverConnect(){
        //połaczenie do serwera -> też kolejna funkcja
        addrinfo hints{}, *res, *rp;
        hints.ai_family = config.ip_version;
        hints.ai_socktype = SOCK_STREAM;

        //FIXME: to jest brzydkie!
        int err = getaddrinfo(config.url.host.c_str(), config.url.port.c_str(), &hints, &res);
        if(err != 0){
            std::cerr << "Bład nawiazania polaczenia: " << std::string(gai_strerror(err));
            return;
        }
        fd = -1;
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
        //TODO: kwestie HTTPS?
    }

public:
    RadioClient(const ClientConfig& cfg) : config(cfg) {}

    void run(){
        serverConnect();
        std::string request = "GET " + config.url.path + " HTTP/1.1\r\n";
        request += "Host: " + config.url.host + "\r\n";
        request += "Connection: Keep-Alive\r\n"; //TODO: czy tak ma być zawsze?
        if(config.request_metadata){
            request += "Icy-MetaData: 1\r\n";
        }
        //TODO: kwestia cookie
        request += "\r\n";
        std::cerr << "wysyłam:\n" << request << "\n";
        safe_send(request);

        std::string status_line = read_http_line();
        std::cerr << "odebrano: " << status_line << "\n";

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
