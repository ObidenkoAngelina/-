#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cstring>
#include <sstream>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <locale.h>

const int BUFFER_SIZE = 16384;
const int PORT = 8888;

std::atomic<bool> running(true);
int sock = -1;
std::string myUsername;
std::string currentChat = "";
std::map<std::string, int> unreadMessages;

static inline void trimCRLF(std::string& s) {
    size_t end = s.find_last_not_of("\n\r");
    if (end == std::string::npos) { s.clear(); return; }
    s.erase(end + 1);
}

static inline void trimSpaces(std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) { s.clear(); return; }
    size_t end = s.find_last_not_of(" \t");
    s = s.substr(start, end - start + 1);
}

void parseServerMessage(const std::string& msg) {
    size_t pos = msg.find('|');
    if (pos == std::string::npos) {
        std::cout << msg << std::endl;
        return;
    }

    std::string type = msg.substr(0, pos);
    std::string data = msg.substr(pos + 1);

    if (type == "UNREAD") {
        unreadMessages.clear();
        std::stringstream ss(data);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (item.empty()) continue;
            size_t colon = item.find(':');
            if (colon == std::string::npos) continue;
            std::string from = item.substr(0, colon);
            int count = 0;
            try { count = std::stoi(item.substr(colon + 1)); }
            catch (...) { count = 0; }
            if (count > 0) {
                unreadMessages[from] = count;
            }
        }
    }
    else if (type == "ALL_USERS") {
        std::cout << "\n=== ВСЕ ПОЛЬЗОВАТЕЛИ ===" << std::endl;
        std::stringstream ss(data);
        std::string user;
        while (std::getline(ss, user, ',')) {
            if (!user.empty() && user != myUsername) {
                auto it = unreadMessages.find(user);
                if (it != unreadMessages.end() && it->second > 0) {
                    std::cout << "  - " << user << " (+" << it->second << " новых)" << std::endl;
                }
                else {
                    std::cout << "  - " << user << std::endl;
                }
            }
        }
        std::cout << "=======================" << std::endl;
        std::cout << "> " << std::flush;
    }
    else if (type == "ONLINE_USERS") {
        std::cout << "\n=== ПОЛЬЗОВАТЕЛИ ОНЛАЙН ===" << std::endl;
        std::stringstream ss(data);
        std::string user;
        while (std::getline(ss, user, ',')) {
            if (!user.empty() && user != myUsername) {
                auto it = unreadMessages.find(user);
                if (it != unreadMessages.end() && it->second > 0) {
                    std::cout << "  - " << user << " (+" << it->second << " новых)" << std::endl;
                }
                else {
                    std::cout << "  - " << user << std::endl;
                }
            }
        }
        std::cout << "=========================" << std::endl;
        std::cout << "> " << std::flush;
    }
    else if (type == "CHAT") {
        std::cout << data << std::endl;
        if (!currentChat.empty()) unreadMessages[currentChat] = 0;
        std::cout << "> " << std::flush;
    }
    else if (type == "MSG") {
        size_t sep = data.find('|');
        if (sep == std::string::npos) return;
        std::string from = data.substr(0, sep);
        std::string text = data.substr(sep + 1);
        if (currentChat == from) {
            std::cout << "\r[" << from << "]: " << text << std::endl;
            unreadMessages[from] = 0;
        }
        else {
            unreadMessages[from]++;
            std::cout << "\r[!] Новых сообщений от " << from << ": " << unreadMessages[from] << std::endl;
        }
        std::cout << "> " << std::flush;
    }
    else if (type == "HISTORY") {
        size_t sep = data.find('|');
        if (sep == std::string::npos) {
            std::cout << "> " << std::flush;
            return;
        }
        std::string chatWith = data.substr(0, sep);
        std::string history = data.substr(sep + 1);
        std::cout << "\n=== ИСТОРИЯ С " << chatWith << " ===" << std::endl;
        if (history.empty()) {
            std::cout << "Нет сообщений" << std::endl;
        }
        else {
            std::stringstream ss(history);
            std::string from, text;
            while (std::getline(ss, from, '|')) {
                if (std::getline(ss, text, '|')) {
                    if (from == myUsername) {
                        std::cout << "[Я]: " << text << std::endl;
                    }
                    else {
                        std::cout << "[" << from << "]: " << text << std::endl;
                    }
                }
            }
        }
        std::cout << "======================" << std::endl;
        unreadMessages[chatWith] = 0;
        std::cout << "> " << std::flush;
    }
    else if (type == "ERROR") {
        std::cout << "[ОШИБКА] " << data << std::endl;
        std::cout << "> " << std::flush;
    }
    else if (type == "SERVER_SHUTDOWN") {
        std::cout << "\n[!!!] СЕРВЕР ОСТАНОВЛЕН [!!!]" << std::endl;
        running = false;
    }
    else {
        std::cout << data << std::endl;
        std::cout << "> " << std::flush;
    }
}

void receiveMessages() {
    char buffer[BUFFER_SIZE];
    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            if (running) std::cout << "\n[!] Отключено от сервера." << std::endl;
            running = false;
            break;
        }
        std::string message(buffer);
        trimCRLF(message);
        if (!message.empty()) parseServerMessage(message);
    }
}

int main() {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    std::cout.imbue(std::locale("ru_RU.UTF-8"));

    std::string server_ip;

    std::cout << "=== МЕССЕНДЖЕР (Linux) ===" << std::endl;
    std::cout << "Введите IP сервера (localhost или IP): ";
    std::getline(std::cin, server_ip);
    if (server_ip.empty()) server_ip = "127.0.0.1";
    if (server_ip == "localhost") server_ip = "127.0.0.1";

    system("mkdir -p logs 2>/dev/null");

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Ошибка: неверный IP" << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "Подключение..." << std::endl;

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "Ошибка: не удалось подключиться" << std::endl;
        close(sock);
        return 1;
    }

    // Цикл ввода имени с проверкой
    char response[256]{};
    bool name_accepted = false;

    std::cout << "\n=== РЕГИСТРАЦИЯ ===" << std::endl;
    std::cout << "Имя может содержать только латинские буквы (A-Z a-z), цифры и символы _ ." << std::endl;

    while (!name_accepted) {
        std::cout << "Введите ваше имя: ";
        std::getline(std::cin, myUsername);

        if (myUsername.empty()) {
            std::cout << "Имя не может быть пустым!" << std::endl;
            continue;
        }

        // Отправляем имя на сервер
        send(sock, myUsername.c_str(), myUsername.length(), 0);

        // Ждём ответ от сервера
        memset(response, 0, 256);
        int recv_len = recv(sock, response, 255, 0);
        if (recv_len <= 0) {
            std::cerr << "Ошибка связи с сервером" << std::endl;
            close(sock);
            return 1;
        }

        std::string answer(response);
        trimCRLF(answer);

        if (answer == "NAME_ACCEPTED") {
            name_accepted = true;
            std::cout << "Имя принято!" << std::endl;
            break;
        }
        else if (answer.rfind("ERROR|", 0) == 0) {
            std::string error_msg = answer.substr(6);
            if (error_msg.find("Пожалуйста, введите другое имя") != std::string::npos) {
                std::cout << "Имя занято или некорректно. Попробуйте другое имя." << std::endl;
            }
            else {
                std::cout << error_msg << std::endl;
            }
        }
        else {
            std::cout << "[ОШИБКА] Неизвестный ответ сервера: " << answer << std::endl;
        }
    }

    // Запускаем поток приёма сообщений
    std::thread receiver(receiveMessages);

    // Небольшая задержка, чтобы поток приёма успел запуститься
    usleep(100000);

    std::cout << "\n=== КОМАНДЫ ===" << std::endl;
    std::cout << "/online_users - список онлайн пользователей" << std::endl;
    std::cout << "/all_users - список ВСЕХ пользователей" << std::endl;
    std::cout << "/chat Имя - начать диалог" << std::endl;
    std::cout << "/quit - выход" << std::endl;
    std::cout << "==============" << std::endl;
    std::cout << std::endl;

    std::string input;
    while (running) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (!running) break;
        if (input.empty()) continue;

        if (input == "/quit") {
            send(sock, input.c_str(), input.length(), 0);
            running = false;
            break;
        }
        else if (input == "/online_users") {
            currentChat = "";
            send(sock, input.c_str(), input.length(), 0);
        }
        else if (input == "/all_users") {
            currentChat = "";
            send(sock, input.c_str(), input.length(), 0);
        }
        else if (input.rfind("/chat", 0) == 0) {
            std::string who = (input.size() > 6) ? input.substr(6) : "";
            trimSpaces(who);
            currentChat = who;
            send(sock, ("/chat " + who).c_str(), 6 + who.size(), 0);
        }
        else {
            if (currentChat.empty()) {
                std::cout << "[ОШИБКА] Вы не в диалоге. Используйте /chat Имя" << std::endl;
            }
            else {
                send(sock, input.c_str(), input.length(), 0);
            }
        }
    }

    if (receiver.joinable()) receiver.join();
    close(sock);
    std::cout << "Отключено." << std::endl;
    return 0;
}