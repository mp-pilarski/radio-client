#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Uzycie: " << argv[0] << " <host> <port> <sciezka>" << endl;
        cerr << "Przyklad: " << argv[0] << " stream.example.com 8000 /stream" << endl;
        return 1;
    }

    string host = argv[1];
    string port_str = argv[2];
    string path = argv[3];
    int port = stoi(port_str);

    // 1. Rozwiązywanie nazwy hosta (DNS)
    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) {
        cerr << "Blad: Nie mozna rozwiazac nazwy hosta " << host << endl;
        return 2;
    }
    cerr << "tworzenie gniazda\n";

    // 2. Tworzenie gniazda (socket)
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "Blad: Nie mozna utworzyc gniazda" << endl;
        return 3;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    cerr << "przed connect\n";
    // 3. Łączenie z serwerem
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Blad: Nie mozna polaczyc sie z serwerem" << endl;
        close(sock);
        return 4;
    }

    cerr << "[*] Polaczono z " << host << ":" << port << endl;

    // 4. Budowanie i wysyłanie żądania HTTP
    // Brak nagłówka 'Icy-MetaData: 1', więc serwer wyśle czyste audio.
    string request = "GET " + path + " HTTP/1.0\r\n";
    request += "Host: " + host + "\r\n";
    request += "User-Agent: Simple-SHOUTcast-Client/1.0\r\n";
    request += "Connection: Keep-Alive\r\n\r\n";

    if (write(sock, request.c_str(), request.length()) < 0) {
        cerr << "Blad: Nie mozna wyslac zadania" << endl;
        close(sock);
        return 5;
    }

    cerr << "[*] Zadanie wyslane, oczekiwanie na odpowiedz..." << endl;

    // 5. Odczyt i pominięcie nagłówków HTTP
    // Czytamy znak po znaku, aż natrafimy na pustą linię (\r\n\r\n)
    string headers = "";
    char c;
    while (read(sock, &c, 1) > 0) {
        headers += c;
        if (headers.length() >= 4 && headers.substr(headers.length() - 4) == "\r\n\r\n") {
            break;
        }
    }
    cerr << "\n================ NAGLOWKI OD SERWERA ================\n";
    cerr << headers;
    cerr << "=====================================================\n";
    cerr << "[*] Naglowki HTTP pominiete. Rozpoczynam pobieranie strumienia audio..." << endl;

    // 6. Pobieranie czystego strumienia audio i zrzucanie go na standardowe wyjście (stdout)
    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(sock, buffer, sizeof(buffer))) > 0) {
        // Zapisujemy odebrane dane audio bezposrednio na stdout
        write(STDOUT_FILENO, buffer, bytes_read);
    }

    cerr << "\n[*] Polaczenie zamkniete przez serwer." << endl;
    close(sock);
    return 0;
}