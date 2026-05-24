#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <map>

#include "common.h"

// Struktura opisująca odpowiedź HTTP
struct HTTPResponse {
    int status_code;
    std::string status_msg;
    // Multimapa par nagłówek - wartość
    // przez to, że może być wiele takich samych nagłówków (np. Set-Cookie)
    std::multimap<std::string, std::string> headers; 

    // Metoda do wyciągania wartości nagłówków
    std::vector<std::string> get_header(const std::string& key) const;
};

// Struktura opisująca sparsowany URL
struct SiteInfo {
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
    bool customPort = false;
};

// Struktura opisująca plik cookie
struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    bool secure = false;
};

// Klasa do zarządzania ciasteczkami
class CookieManager {
private:
    std::vector<Cookie> cookies;

    void add_or_update_cookie(const Cookie& new_cookie);
    void parse_and_store(const std::string& header_val, const std::string& default_domain);

public:
    void update_from_response(const HTTPResponse& response, const std::string& request_domain);
    std::string get_cookie_header(const std::string& domain, const std::string& path, bool is_secure_connection);
};

HTTPResponse parse_http_response(const std::string& raw_input);
std::string resolve_redirect(const std::string& location, const SiteInfo& url);
SiteInfo parse_url(std::string& url);