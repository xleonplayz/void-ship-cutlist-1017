#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#ifndef __wasi__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

const unsigned RELEASE = 1;
const unsigned KERF = 0;

std::string failure(const std::string &message) {
    return "{\"ok\":false,\"error\":\"" + message + "\",\"release\":" + std::to_string(RELEASE) + "}";
}

bool number(const std::string &text, unsigned &value) {
    if (text.empty() || text.size() > 6) return false;
    value = 0;
    for (char c : text) { if (c < '0' || c > '9') return false; value = value * 10 + unsigned(c - '0'); }
    return true;
}

std::string plan(const std::string &stock_text, const std::string &pieces_text) {
    unsigned stock = 0;
    if (!number(stock_text, stock) || stock < 100 || stock > 100000) return failure("stock must be 100..100000 mm");
    if (pieces_text.empty() || pieces_text.back() == ',') return failure("piece list is empty or incomplete");
    std::vector<unsigned> pieces;
    std::istringstream input(pieces_text);
    std::string part;
    while (std::getline(input, part, ',')) {
        unsigned value = 0;
        if (!number(part, value) || value == 0) return failure("piece must be a positive integer");
        if (value + KERF > stock) return failure("piece exceeds stock including kerf");
        pieces.push_back(value);
        if (pieces.size() > 100) return failure("at most 100 pieces");
    }
    std::sort(pieces.begin(), pieces.end(), [](unsigned a, unsigned b) { return a > b; });
    std::vector<unsigned> remaining;
    unsigned material = 0;
    for (unsigned piece : pieces) {
        material += piece;
        bool placed = false;
        for (auto &left : remaining) {
            if (left >= piece + KERF) { left -= piece + KERF; placed = true; break; }
        }
        if (!placed) remaining.push_back(stock - piece - KERF);
    }
    unsigned waste = 0;
    for (unsigned left : remaining) waste += left;
    std::ostringstream out;
    out << "{\"ok\":true,\"service\":\"cpp-cutlist\",\"release\":" << RELEASE
        << ",\"stockMm\":" << stock << ",\"pieces\":" << pieces.size()
        << ",\"bars\":" << remaining.size() << ",\"materialMm\":" << material
        << ",\"kerfMm\":" << KERF * pieces.size() << ",\"wasteMm\":" << waste << ",\"remainingMm\":[";
    for (unsigned i = 0; i < remaining.size(); ++i) { if (i) out << ','; out << remaining[i]; }
    out << "]}";
    return out.str();
}

std::string answer(const std::string &raw) {
    std::istringstream request(raw);
    std::string method, path;
    request >> method >> path;
    if (path.empty()) path = "/health";
    path = path.substr(0, path.find('?'));
    std::cerr << "cutlist " << method << ' ' << path << " release=" << RELEASE << '\n';
    if (path == "/" || path == "/health") return "{\"ok\":true,\"service\":\"cpp-cutlist\",\"release\":" + std::to_string(RELEASE) + "}";
    if (method == "GET" && path.find("/plan/") == 0) {
        auto slash = path.find('/', 6);
        if (slash == std::string::npos) return failure("expected plan/stock/pieces");
        return plan(path.substr(6, slash - 6), path.substr(slash + 1));
    }
    if (method == "POST" && path == "/plan") {
        auto body_start = raw.find("\r\n\r\n");
        if (body_start == std::string::npos) return failure("request body missing");
        std::istringstream body(raw.substr(body_start + 4));
        std::string stock, pieces;
        if (!std::getline(body, stock) || !std::getline(body, pieces)) return failure("expected stock then pieces on next line");
        if (!stock.empty() && stock.back() == '\r') stock.pop_back();
        if (!pieces.empty() && pieces.back() == '\r') pieces.pop_back();
        return plan(stock, pieces);
    }
    return failure("unknown route or method");
}

#ifndef __wasi__
int serve() {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1; setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(8240); addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (server < 0 || bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) || listen(server, 16)) return 1;
    while (true) {
        int client = accept(server, nullptr, nullptr);
        if (client < 0) continue;
        timeval timeout{5, 0}; setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        std::string raw;
        while (raw.size() < 16384) {
            char buf[4096]; auto n = recv(client, buf, sizeof(buf), 0); if (n <= 0) break;
            raw.append(buf, n); auto end = raw.find("\r\n\r\n");
            if (end == std::string::npos) continue;
            std::string header = raw.substr(0, end);
            std::transform(header.begin(), header.end(), header.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            auto offset = header.find("content-length:"); unsigned length = 0;
            if (offset != std::string::npos) { std::istringstream value(header.substr(offset + 15)); value >> length; }
            if (raw.size() >= end + 4 + length) break;
        }
        std::string body = answer(raw);
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        size_t sent = 0;
        while (sent < response.size()) { auto n = send(client, response.data() + sent, response.size() - sent, MSG_NOSIGNAL); if (n <= 0) break; sent += n; }
        close(client);
    }
}
#endif

int main(int argc, char **argv) {
#ifndef __wasi__
    if (argc == 2 && std::string(argv[1]) == "--serve") return serve();
#endif
    std::string raw((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    std::cout << answer(raw) << '\n';
}
