#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <algorithm>

using namespace std;

// Funkcja pomocnicza: konwersja stringa na małe litery
string toLowerCase(string str) {
    transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

// Funkcja wyciągająca tytuł piosenki z surowego łańcucha metadanych
void parseAndPrintMetadata(const string& meta) {
  cerr << "Nowe metadane: " << meta << "\n";
    // Szukamy frazy StreamTitle='
    string searchStr = "streamtitle='";
    string lowerMeta = toLowerCase(meta);
    size_t pos = lowerMeta.find(searchStr);
    
    if (pos != string::npos) {
        size_t start = pos + searchStr.length();
        // Szukamy zamykającego apostrofu
        size_t end = meta.find("'", start);
        if (end != string::npos) {
            string title = meta.substr(start, end - start);
            if (!title.empty()) {
                // Wypisujemy na stderr, żeby nie śmiecić w strumieniu audio (stdout)
                cerr << "\n[METADANE] Teraz gramy: \033[1;32m" << title << "\033[0m" << endl;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Uzycie: " << argv[0] << " <host> <port> <sciezka>" << endl;
        cerr << "Przyklad: " << argv[0] << " stream.example.com 8000 /" << endl;
        return 1;
    }

    string host = argv[1];
    int port = stoi(argv[2]);
    string path = argv[3];

    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) { cerr << "Blad DNS." << endl; return 2; }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Blad polaczenia." << endl; return 4;
    }

    // WYSYŁAMY ŻĄDANIE Z NAGŁÓWKIEM Icy-MetaData: 1
    string request = "GET " + path + " HTTP/1.0\r\n";
    request += "Host: " + host + "\r\n";
    request += "User-Agent: WinampMPEG/5.0\r\n";
    request += "Icy-MetaData: 1\r\n"; // <-- PROSIMY O METADANE
    request += "Connection: close\r\n\r\n";

    write(sock, request.c_str(), request.length());

    // ODCZYT NAGŁÓWKÓW
    string headers = "";
    char c;
    while (read(sock, &c, 1) > 0) {
        headers += c;
        if (headers.length() >= 4 && headers.substr(headers.length() - 4) == "\r\n\r\n") break;
    }

    // SZUKANIE icy-metaint (interwału metadanych)
    int icy_metaint = 0;
    string lowerHeaders = toLowerCase(headers);
    size_t metaintPos = lowerHeaders.find("icy-metaint:");
    if (metaintPos != string::npos) {
        size_t start = metaintPos + 12; // dlugosc "icy-metaint:"
        size_t end = lowerHeaders.find("\r\n", start);
        icy_metaint = stoi(headers.substr(start, end - start));
        cerr << "[*] Znaleziono icy-metaint: " << icy_metaint << " bajtow." << endl;
    } else {
        cerr << "[!] Serwer nie obsluguje metadanych lub nie wyslal icy-metaint." << endl;
    }

    // MASZYNA STANÓW DO MULTIPLEKSOWANIA
    enum State { READ_AUDIO, READ_META_LENGTH, READ_META_DATA };
    State state = READ_AUDIO;
    
    int audio_bytes_left = icy_metaint;
    int meta_bytes_left = 0;
    string meta_buffer = "";

    // Bufor dla czystego audio (aby uniknąć write() dla każdego pojedynczego bajtu)
    unsigned char out_audio[8192];
    int out_audio_pos = 0;

    auto flush_audio = [&]() {
        if (out_audio_pos > 0) {
            write(STDOUT_FILENO, out_audio, out_audio_pos);
            out_audio_pos = 0;
        }
    };

    unsigned char buffer[4096];
    ssize_t bytes_read;

    cerr << "[*] Rozpoczynam odtwarzanie i nasluchiwanie metadanych..." << endl;

    while ((bytes_read = read(sock, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; ++i) {
            unsigned char byte = buffer[i];

            if (state == READ_AUDIO) {
                // Jesli nie ma metadanych (icy_metaint == 0), po prostu kopiujemy w nieskonczonosc
                if (icy_metaint == 0) {
                    out_audio[out_audio_pos++] = byte;
                    if (out_audio_pos == sizeof(out_audio)) flush_audio();
                    continue;
                }

                // Odliczanie bajtów audio do następnego bloku metadanych
                out_audio[out_audio_pos++] = byte;
                if (out_audio_pos == sizeof(out_audio)) flush_audio();

                audio_bytes_left--;
                if (audio_bytes_left == 0) {
                    flush_audio(); // Wypychamy zgromadzone audio przed wejściem w metadane
                    state = READ_META_LENGTH;
                }
            } 
            else if (state == READ_META_LENGTH) {
                // Pojedynczy bajt mówi nam o długości metadanych (mnożnik x 16)
                meta_bytes_left = byte * 16;
                meta_buffer = "";

                if (meta_bytes_left > 0) {
                    state = READ_META_DATA;
                } else {
                    cerr << ".";
                    // 0 oznacza, że w tym cyklu nie ma nowych metadanych
                    state = READ_AUDIO;
                    audio_bytes_left = icy_metaint;
                }
            } 
            else if (state == READ_META_DATA) {
                // Czytanie tekstu metadanych
                if (byte != '\0') { // ignorujemy padding zerami
                    meta_buffer += (char)byte;
                }
                meta_bytes_left--;

                if (meta_bytes_left == 0) {
                    parseAndPrintMetadata(meta_buffer);
                    state = READ_AUDIO;
                    audio_bytes_left = icy_metaint;
                }
            }
        }
    }

    flush_audio();
    cerr << "\n[*] Koniec strumienia." << endl;
    close(sock);
    return 0;
}
