#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <map>

#include "common.h"

struct HTTPResponse {
    int status_code;
    std::string status_msg;
    std::multimap<std::string, std::string> headers; //multimap przez to, że może być wiele takich samych nagłówków

    // Metoda do wyciągania wartości nagłówków
    std::vector<std::string> get_header(const std::string& key) const;
};


struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    bool secure = false;
};

class CookieManager {
private:
    std::vector<Cookie> cookies;

    // Ciasteczka aktualizuje się na podstawie nazwy, domeny i ścieżki.
    // Jeśli takie już istnieje, nadpisujemy je.
    void add_or_update_cookie(const Cookie& new_cookie);

public:
    // 1. ODBIERALNIA: Pobiera ciasteczka z HTTPResponse
    void update_from_response(const HTTPResponse& response, const std::string& request_domain);

    // 2. WYSYŁALNIA: Generuje string do nagłówka "Cookie" dla nowego żądania
    std::string get_cookie_header(const std::string& domain, const std::string& path, bool is_secure_connection);

private:

    // Parsowanie pojedynczego nagłówka Set-Cookie
    void parse_and_store(const std::string& header_val, const std::string& default_domain);
};