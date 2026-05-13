#include "HttpUploader.h"
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

// Parse URL into host, port, path
static bool parseUrl(const std::string& url, std::string& host, std::string& port, std::string& path) {
    // Expect http://host:port/path or http://host/path
    size_t start = 0;
    if (url.substr(0, 7) == "http://") {
        start = 7;
    } else if (url.substr(0, 8) == "https://") {
        start = 8;
    }

    size_t pathStart = url.find('/', start);
    if (pathStart == std::string::npos) {
        path = "/";
        pathStart = url.size();
    } else {
        path = url.substr(pathStart);
    }

    std::string hostPort = url.substr(start, pathStart - start);
    size_t colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        host = hostPort.substr(0, colonPos);
        port = hostPort.substr(colonPos + 1);
    } else {
        host = hostPort;
        port = "80";
    }

    return !host.empty();
}

void HttpUploader::uploadJson(const std::string& url, const std::string& jsonBody,
                              std::function<void(bool success, int statusCode, const std::string& response)> callback) {
    std::string host, port, path;
    if (!parseUrl(url, host, port, path)) {
        if (callback) callback(false, 0, "Invalid URL");
        return;
    }

    // Resolve host
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (err != 0 || !res) {
        if (callback) callback(false, 0, "DNS resolution failed");
        return;
    }

    // Create socket and connect
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        if (callback) callback(false, 0, "Socket creation failed");
        return;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(sock);
        if (callback) callback(false, 0, "Connection failed");
        return;
    }
    freeaddrinfo(res);

    // Build HTTP request
    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << jsonBody.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << jsonBody;

    std::string request = req.str();

    // Send
    ssize_t totalSent = 0;
    while (totalSent < static_cast<ssize_t>(request.size())) {
        ssize_t sent = send(sock, request.c_str() + totalSent,
                           request.size() - totalSent, 0);
        if (sent <= 0) {
            close(sock);
            if (callback) callback(false, 0, "Send failed");
            return;
        }
        totalSent += sent;
    }

    // Read response
    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    close(sock);

    // Parse status code from first line: "HTTP/1.1 201 Created"
    int statusCode = 0;
    size_t spacePos = response.find(' ');
    if (spacePos != std::string::npos) {
        statusCode = std::atoi(response.c_str() + spacePos + 1);
    }

    // Extract body (after \r\n\r\n)
    std::string body;
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        body = response.substr(headerEnd + 4);
    }

    bool success = (statusCode >= 200 && statusCode < 300);
    if (callback) callback(success, statusCode, body);
}
