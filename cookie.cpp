#include "cookie.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "common.h"

#include <iostream> //TODO: do usuniecia
#include <sstream>

SiteInfo parse_url(std::string& url){
    SiteInfo result;
    //TODO: usprawnić! + szerszy error handling!

    // Fragment po # jest zignorowany
    std::string temp = url;
    auto hash_pos = url.find('#');
    if(hash_pos != std::string::npos) {
        temp = temp.substr(0, hash_pos);
    }

    // Szukanie scheme
    auto colon_pos = temp.find(':');
    auto slash_pos = temp.find('/');

    if(colon_pos != std::string::npos && (slash_pos == std::string::npos || colon_pos < slash_pos)){
        result.scheme = temp.substr(0, colon_pos);
        temp = temp.substr(colon_pos + 1);
    }
    // Jedne akceptowane scheme to http i https
    if(result.scheme != "http" && result.scheme != "https"){
        throw std::runtime_error("Unsupported protocol");
    }

    if(temp.length() >= 2 && temp[0] == '/' && temp[1] == '/'){
        temp = temp.substr(2);

        std::string host;
        // Szukanie path
        slash_pos = temp.find('/');
        if(slash_pos != std::string::npos) {
            host = temp.substr(0, slash_pos);
            result.path = temp.substr(slash_pos);
        }else{
            host = temp;
            result.path = "/";
        }
        
        // Host to adres IPv6
        if(!host.empty() && host[0] == '['){
            auto bracket_end = host.find(']');
            if(bracket_end != std::string::npos){
                result.host = host.substr(0, bracket_end+1);
                host = host.substr(bracket_end+1);

                // Wyciągnięcie portu
                if(!host.empty() && host[0] == ':'){
                    result.port = host.substr(1);
                    result.customPort = true;
                }else{
                    result.port = (result.scheme == "https") ? "443" : "80";
                }
            }
        }else {
            // Wyciągnięcie portu
            auto port_colon = host.find(':');
            if(port_colon != std::string::npos) {
                result.host = host.substr(0, port_colon);
                result.port = host.substr(port_colon+1);
                result.customPort = true;
            }else{
                //
                result.host = host;
                result.port = (result.scheme == "https") ? "443" : "80";
            }
        }
    }
    return result;
}

// Parsowanie stringa z http response na strukturę HTTPResponse
HTTPResponse parse_http_response(const std::string& raw_input){
    HTTPResponse response{};
    std::istringstream stream(raw_input);
    std::string line;

    // Pierwsza linia to wersja i status
    if(std::getline(stream, line)){
        trim(line);
        size_t space1 = line.find(' ');
        size_t space2 = line.find(' ', space1 + 1);

        if (space1 != std::string::npos && space2 != std::string::npos) {
            try{
                response.status_code = std::stoi(line.substr(space1 + 1, space2 - space1 - 1));
            }catch(std::exception& e){
                throw std::runtime_error("Error during reading status code");
            }
            response.status_msg = line.substr(space2 + 1);
        }
    }

    // Kolejne linie to pary nazwa: wartosc
    while(std::getline(stream, line)){
        trim(line);
        if(line.empty()) continue;
        size_t colon_pos = line.find(':');
        if(colon_pos != std::string::npos){
            std::string key = line.substr(0, colon_pos);
            std::string val = line.substr(colon_pos + 1);
            trim(key);
            trim(val);
            stringToLower(key);
            response.headers.emplace(key, val);
        }
    }

    return response;
}

// Funkcja do rozwiązywania celu redirecta
std::string resolve_redirect(const std::string& location, const SiteInfo& url){
    // Pełen url
    if(location.find("http://") == 0 || location.find("https://") == 0){
        return location;
    }
    // taki sam host, ale inna sciezka
    std::string new_loc = url.scheme + "://" + url.host;
    if(url.customPort){
        new_loc += ":" + url.port;
    }
    if(!location.empty() && location[0] == '/'){
        // Sciezka bezwzgledna
        new_loc += location;
    }else{
        // Sciezka wzgledna
        std::string base_path = url.path;
        auto slash = base_path.find_last_of('/');
        if (slash != std::string::npos) {
            new_loc += base_path.substr(0, slash + 1) + location;
        } else {
            new_loc += "/" + location;
        }
    }
    return new_loc;
}

// Metoda do wyciągania wartości nagłówków
std::vector<std::string> HTTPResponse::get_header(const std::string& key) const {
    std::vector<std::string> result;
    auto range = headers.equal_range(key);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }
    // Dla uproszczenia głównej części implementacji, jeśli nie ma danego nagłówka jest zwracany wektor z pustym napisem
    if (result.empty()) {
        result.push_back("");
    }
    return result;
}

// Aktualizowanie zbioru ciasteczek
void CookieManager::add_or_update_cookie(const Cookie& new_cookie) {
    for (auto& c : cookies) {
        if (c.name == new_cookie.name && c.domain == new_cookie.domain &&
            c.path == new_cookie.path) {
            c = new_cookie;  // Nadpisz istniejące ciasteczko
            return;
        }
    }
    cookies.push_back(new_cookie);
}

void CookieManager::update_from_response(const HTTPResponse& response, const std::string& request_domain) {
    auto set_cookies = response.get_header("set-cookie");

    for (const auto& cookie_str : set_cookies) {
        if(cookie_str != ""){
            parse_and_store(cookie_str, request_domain);
        }
    }
}

// Funkcja do generowania stringa do nagłówka "Cookie" dla nowego żądania
std::string CookieManager::get_cookie_header(const std::string& domain, const std::string& path, bool is_secure_connection) {
    std::string header_value = "";
    for (const auto& c : cookies) {
        // Dopasowanie domeny
        bool domain_match = (domain.find(c.domain) != std::string::npos);

        // Dopasowanie ścieżki
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
void CookieManager::parse_and_store(const std::string& header_val, const std::string& default_domain) {
    auto parts = split(header_val, ';');
    if (parts.empty()) return;

    Cookie c;
    c.domain = default_domain;  // Domyślna domena to ta, do której wysłano request
    c.path = "/"; // Domyślna ścieżka

    // Pierwszy element to Nazwa=Wartość
    auto eq_pos = parts[0].find('=');
    if(eq_pos != std::string::npos){
        c.name = parts[0].substr(0, eq_pos);
        c.value = parts[0].substr(eq_pos+1);
    }

    // Parsowanie pozostałych atrybutów
    for (size_t i = 1; i < parts.size(); ++i) {
        std::string attr = parts[i];
        stringToLower(attr);

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