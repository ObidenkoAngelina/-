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
#include <locale.h>

const int BUFFER_SIZE = 16384;
const int PORT = 8888;

std::atomic<bool> running(true);
int sockfd = -1;
std::string myUsername;
std::string currentChat = "";
std::map<std::string, int> unreadMessages;

static inline bool isValidUsername(const std::string& name) {
    if (name.empty()) return false;
    if (name.size() > 32) return false;
    for (unsigned char ch : name) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) continue;
        if (ch >= '0' && ch <= '9') continue;
        if (ch == '_' || ch == '.') continue;
        return false;
    }
    return true;
}

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
        std::cout << msg << "\n> " << std::flush;
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
            if (count > 0) unreadMessages[from] = count;
        }
    }
    else if (type == "ALL_USERS") {
        std::cout << "\n=== ВСЕ ПОЛЬЗОВАТЕЛИ ===\n";
        std::stringstream ss(data);
        std::string user;
        while (std::getline(ss, user, ',')) {
            if (user.empty() || user == myUsername) continue;
            int c = unreadMessages[user];
            if (c > 0) std::cout << "  - " << user << " (+" << c << " новых)\n";
            else std::cout << "  - " << user << "\n";
        }
        std::cout << "=======================\n> " << std::flush;
    }
    else if (type == "ONLINE_USERS") {
        std::cout << "\n=== ПОЛЬЗОВАТЕЛИ ОНЛАЙН ===\n";
        std::stringstream ss(data);
        std::string user;
        while (std::getline(ss, user, ',')) {
            if (user.empty() || user == myUsername) continue;
            int c = unreadMessages[user];
            if (c > 0) std::cout << "  - " << user << " (+" << c << " новых)\n";
            else std::cout << "  - " << user << "\n";
        }
        std::cout << "=========================\n> " << std::flush;
    }
    else if (type == "CHAT") {
        std::cout << data << "\n> " << std::flush;
    }
    else if (type == "MSG") {
        size_t sep = data.find('|');
        if (sep == std::string::npos) return;
        std::string from = data.substr(0, sep);
        std::string text = data.substr(sep + 1);
        if (currentChat == from) {
            std::cout << "\r[" << from << "]: " << text << "\n";
            unreadMessages[from] = 0;
        }
        else {
            unreadMessages[from]++;
            std::cout << "\r[!] Новых сообщений от " << from << ": " << unreadMessages[from] << "\n";
        }
        std::cout << "> " << std::flush;
    }
    else if (type == "HISTORY") {
        size_t sep = data.find('|');
        if (sep == std::string::npos) { std::cout << "> " << std::flush; return; }
        std::string chatWith = data.substr(0, sep);
        std::string history = data.substr(sep + 1);

        std::cout << "\n=== ИСТОРИЯ С " << chatWith << " ===\n";
        if (history.empty()) std::cout << "Нет сообщений\n";
        else {
            std::stringstream ss(history);
            std::string from, text;
            while (std::getline(ss, from, '|')) {
                if (std::getline(ss, text, '|')) {
                    if (from == myUsername) std::cout << "[Я]: " << text << "\n";
                    else std::cout << "[" << from << "]: " << text << "\n";
                }
            }
        }
        std::cout << "======================\n";
        unreadMessages[chatWith] = 0;
        std::cout << "> " << std::flush;
    }
    else if (type == "ERROR") {
        std::cout << "[ОШИБКА] " << data << "\n> " << std::flush;
    }
    else if (type == "SERVER_SHUTDOWN") {
        std::cout << "\n[!!!] СЕРВЕР ОСТАНОВЛЕН [!!!]\n";
        running = false;
    }
    else {
        std::cout << data << "\n> " << std::flush;
    }
}

void receiveMessages() {
    char buffer[BUFFER_SIZE];
    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (n <= 0) { running = false; break; }
        buffer[n] = '\0';
        std::string msg(buffer);
        trimCRLF(msg);
        if (!msg.empty()) parseServerMessage(msg);
    }
}

int main() {
    setlocale(LC_ALL, "");

    std::string server_ip;
    std::cout << "=== МЕССЕНДЖЕР (Linux) ===\n";
    std::cout << "Введите IP сервера (localhost или IP): ";
    std::getline(std::cin, server_ip);
    if (server_ip.empty() || server_ip == "localhost") server_ip = "127.0.0.1";

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) { std::cerr << "Ошибка создания сокета\n"; return 1; }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Неверный IP\n";
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "Не удалось подключиться\n";
        close(sockfd);
        return 1;
    }

    // --- регистрация: отправляем имя сразу ---
    std::cout << "\n=== РЕГИСТРАЦИЯ ===\n";
    std::cout << "Имя: A-Z a-z 0-9 _ . (до 32)\n";

    while (true) {
        std::cout << "Введите ваше имя: ";
        std::getline(std::cin, myUsername);
        trimSpaces(myUsername);

        if (!isValidUsername(myUsername)) {
            std::cout << "ОШИБКА: имя некорректно.\n";
            continue;
        }

        std::string toSend = myUsername + "\n";
        send(sockfd, toSend.c_str(), (int)toSend.size(), 0);

        char resp[256]{};
        int rn = recv(sockfd, resp, 255, 0);
        if (rn <= 0) { std::cerr << "Сервер недоступен\n"; close(sockfd); return 1; }
        resp[rn] = '\0';

        std::string answer(resp);
        trimCRLF(answer);

        if (answer == "NAME_ACCEPTED") {
            std::cout << "Имя принято!\n";
            break;
        }
        if (answer.rfind("ERROR|", 0) == 0) {
            std::cout << answer.substr(6) << "\n";
            continue;
        }
        std::cout << "Неизвестный ответ: " << answer << "\n";
    }

    std::thread receiver(receiveMessages);

    std::cout << "\n=== КОМАНДЫ ===\n"
        << "/online_users\n"
        << "/all_users\n"
        << "/chat Имя\n"
        << "/quit\n"
        << "==============\n\n";

    std::string input;
    while (running) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (!running) break;
        if (input.empty()) continue;

        if (input == "/quit") {
            send(sockfd, (input + "\n").c_str(), (int)input.size() + 1, 0);
            running = false;
            break;
        }
        else if (input == "/online_users" || input == "/all_users") {
            currentChat.clear();
            std::string msg = input + "\n";
            send(sockfd, msg.c_str(), (int)msg.size(), 0);
        }
        else if (input.rfind("/chat", 0) == 0) {
            std::string who = (input.size() > 6) ? input.substr(6) : "";
            trimSpaces(who);
            if (!isValidUsername(who)) { std::cout << "Некорректное имя\n"; continue; }
            currentChat = who;
            std::string msg = "/chat " + who + "\n";
            send(sockfd, msg.c_str(), (int)msg.size(), 0);
        }
        else {
            if (currentChat.empty()) {
                std::cout << "Вы не в диалоге. Используйте /chat Имя\n";
            }
            else {
                std::string msg = input + "\n";
                send(sockfd, msg.c_str(), (int)msg.size(), 0);
            }
        }
    }

    running = false;
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    if (receiver.joinable()) receiver.join();
    return 0;
}
