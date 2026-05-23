#include "cookie.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "common.h"

#include <iostream> //TODO: do usuniecia

// Metoda do wyciągania wartości nagłówków
std::vector<std::string> HTTPResponse::get_header(const std::string& key) const {
    std::vector<std::string> result;
    auto range = headers.equal_range(key);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }
    if (result.empty()) {
        result.push_back("");
    }
    return result;
    // auto it = headers.find(key);
    // return it != headers.end() ? it->second : "";
}

// Ciasteczka aktualizuje się na podstawie nazwy, domeny i ścieżki.
// Jeśli takie już istnieje, nadpisujemy je.
void CookieManager::add_or_update_cookie(const Cookie& new_cookie) {
    for (auto& c : cookies) {
        if (c.name == new_cookie.name && c.domain == new_cookie.domain &&
            c.path == new_cookie.path) {
            c = new_cookie;  // Nadpisz istniejące
            return;
        }
    }
    cookies.push_back(new_cookie);
}

void CookieManager::update_from_response(const HTTPResponse& response,
                                         const std::string& request_domain) {
    auto set_cookies = response.get_header("set-cookie");

    for (const auto& cookie_str : set_cookies) {
        parse_and_store(cookie_str, request_domain);
    }
}

// 2. WYSYŁALNIA: Generuje string do nagłówka "Cookie" dla nowego żądania
std::string CookieManager::get_cookie_header(const std::string& domain,
                                             const std::string& path,
                                             bool is_secure_connection) {
    std::string header_value = "";
    for (const auto& c : cookies) {
        // Logika dopasowania domeny (uproszczona)
        // W pełnym RFC: domena "example.com" pasuje do "sub.example.com"
        bool domain_match = (domain.find(c.domain) != std::string::npos);

        // Logika dopasowania ścieżki
        bool path_match = (path.find(c.path) == 0);

        bool secure_match = !c.secure || is_secure_connection;

        if (domain_match && path_match && secure_match) {
            if (!header_value.empty()) {
                header_value += "; ";
            }
            header_value += c.name + "=" + c.value;
        }
    }
    return header_value;
}

// Parsowanie pojedynczego nagłówka Set-Cookie
void CookieManager::parse_and_store(const std::string& header_val,
                                    const std::string& default_domain) {
    auto parts = split(header_val, ';');
    if (parts.empty()) return;

    Cookie c;
    c.domain = default_domain;  // Domyślna domena to ta, do której wysłano request
    c.path = "/";        // Domyślna ścieżka

    // Pierwszy element to ZAWSZE Nazwa=Wartość
    auto eq_pos = parts[0].find('=');
    if(eq_pos != std::string::npos){
        c.name = parts[0].substr(0, eq_pos);
        c.value = parts[0].substr(eq_pos+1);
    }

    // Parsowanie pozostałych atrybutów
    for (size_t i = 1; i < parts.size(); ++i) {
        std::string attr = parts[i];
        std::transform(attr.begin(), attr.end(), attr.begin(),
                       ::tolower);  // Case-insensitive

        if (attr.find("domain=") == 0) {
            c.domain = parts[i].substr(7);
        } else if (attr.find("path=") == 0) {
            c.path = parts[i].substr(5);
        } else if (attr == "secure") {
            c.secure = true;
        }
    }

    add_or_update_cookie(c);
}