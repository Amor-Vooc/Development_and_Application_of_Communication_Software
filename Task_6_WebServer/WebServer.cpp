/**
 * Simple C++ HTTP Server (Winsock)
 * 功能：支持静态文件服务 (GET) 和简单的表单回显 (POST)
 * 特性：多线程处理、MIME类型检测、URL解码、路径遍历防御、中文控制台输出支持
 */

#define _WINSOCK_DEPRECATED_NO_WARNINGS

 // ==========================================
 // Include Headers
 // ==========================================
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <iomanip>

// Windows Socket Headers
#include <winsock2.h>
#include <ws2tcpip.h>

// 链接 ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;

// ==========================================
// Constants & Globals
// ==========================================
const int DEFAULT_PORT = 9000;
const std::string DEFAULT_ROOT = ".";

// ==========================================
// Helper Functions
// ==========================================

/// <summary>
/// 根据文件后缀获取 MIME 类型
/// </summary>
std::string get_mime_type(const std::string& extension) {
    static const std::map<std::string, std::string> mime_types = {
        {".html", "text/html"},
        {".htm",  "text/html"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".txt",  "text/plain"},
        {".jpg",  "image/jpeg"},
        {".png",  "image/png"}
    };
    auto it = mime_types.find(extension);
    if (it != mime_types.end()) return it->second;
    return "text/plain";
}

/// <summary>
/// 简单的 HTML 转义，防止 XSS 或页面结构破坏
/// </summary>
std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;        break;
        }
    }
    return out;
}

/// <summary>
/// UTF-8 转 ANSI (用于 Windows 控制台正常显示中文)
/// </summary>
std::string Utf8ToAnsi(const std::string& utf8) {
    // 1. UTF-8 -> UTF-16 (Wide Char)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (wlen == 0) return "";
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wbuf.data(), wlen);

    // 2. UTF-16 -> ANSI (系统默认 Codepage，中文环境通常是 GBK)
    int len = WideCharToMultiByte(CP_ACP, 0, wbuf.data(), -1, NULL, 0, NULL, NULL);
    if (len == 0) return "";
    std::vector<char> buf(len);
    WideCharToMultiByte(CP_ACP, 0, wbuf.data(), -1, buf.data(), len, NULL, NULL);

    return std::string(buf.data());
}

/// <summary>
/// URL 解码 (例如: %20 -> 空格)
/// </summary>
std::string url_decode(const std::string& encoded) {
    std::string decoded;
    decoded.reserve(encoded.length());
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            std::string hex = encoded.substr(i + 1, 2);
            char ch = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            decoded += ch;
            i += 2;
        }
        else if (encoded[i] == '+') {
            decoded += ' ';
        }
        else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

/// <summary>
/// 解析 POST Body (格式: key1=val1&key2=val2)
/// </summary>
std::map<std::string, std::string> parse_post_body(const std::string& body) {
    std::map<std::string, std::string> params;
    std::stringstream ss(body);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = url_decode(pair.substr(0, pos));
            std::string val = url_decode(pair.substr(pos + 1));
            params[key] = val;
        }
    }
    return params;
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

// ==========================================
// Core Logic
// ==========================================

void handle_client(SOCKET clientSocket, std::string rootDir) {
    char buffer[8192];
    int bytesReceived = recv(clientSocket, buffer, 8192, 0);

    if (bytesReceived <= 0) {
        closesocket(clientSocket);
        return;
    }

    // 1. 解析请求行
    std::string rawRequest(buffer, bytesReceived);
    HttpRequest request;
    std::istringstream stream(rawRequest);
    std::string line;

    if (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        lineStream >> request.method >> request.path;
    }

    // 2. 预处理路径
    // 移除 query string (?后面的内容)
    size_t qpos = request.path.find('?');
    if (qpos != std::string::npos) request.path = request.path.substr(0, qpos);

    // URL 解码
    std::string decodedPath = url_decode(request.path);

    // 去掉起始的 '/' 或 '\'，转换为相对路径
    while (!decodedPath.empty() && (decodedPath[0] == '/' || decodedPath[0] == '\\')) {
        decodedPath.erase(0, 1);
    }

    if (decodedPath.empty()) decodedPath = "index.html";

    // 打印请求日志
    std::cout << "[Thread " << std::this_thread::get_id() << "] "
        << request.method << " request for: " << request.path << std::endl;

    std::string response;

    try {
        fs::path rootPath(rootDir);
        fs::path targetPath = fs::weakly_canonical(rootPath / decodedPath);

        // 3. 安全检查：防止路径遍历攻击 (Path Traversal)
        // 确保最终解析的路径必须以 rootPath 开头
        std::string rootStr = fs::canonical(rootPath).string();
        std::string targetStr = targetPath.string();

        if (targetStr.size() < rootStr.size() || targetStr.compare(0, rootStr.size(), rootStr) != 0) {
            // 访问 root 之外的路径 -> 403 Forbidden
            std::string bodyContent = "<html><head><meta charset='utf-8'></head><body><h1>403 Forbidden</h1><p>Access denied.</p></body></html>";
            response = "HTTP/1.1 403 Forbidden\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: " + std::to_string(bodyContent.size()) + "\r\n\r\n" +
                bodyContent;

            send(clientSocket, response.c_str(), (int)response.size(), 0);
            closesocket(clientSocket);
            return;
        }

        // 4. 处理 GET 请求
        if (request.method == "GET") {
            if (fs::exists(targetPath) && fs::is_regular_file(targetPath)) {
                // 文件存在且是普通文件 -> 返回文件内容
                std::ifstream file(targetPath, std::ios::binary);
                if (file) {
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string content = buffer.str();
                    std::string mime = get_mime_type(fs::path(targetPath).extension().string());

                    response = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: " + mime + "\r\n"
                        "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n" +
                        content;
                }
                else {
                    response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                }
            }
            else if (fs::exists(targetPath) && fs::is_directory(targetPath)) {
                // 如果是目录，尝试查找 index.html
                fs::path indexPath = targetPath / "index.html";
                if (fs::exists(indexPath) && fs::is_regular_file(indexPath)) {
                    std::ifstream file(indexPath, std::ios::binary);
                    if (file) {
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        std::string content = buffer.str();
                        std::string mime = get_mime_type(fs::path(indexPath).extension().string());

                        response = "HTTP/1.1 200 OK\r\n"
                            "Content-Type: " + mime + "\r\n"
                            "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n" +
                            content;
                    }
                    else {
                        response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                    }
                }
                else {
                    // 目录存在但没有 index -> 返回 403（禁止列目录）
                    std::string bodyContent = "<html><head><meta charset='utf-8'></head><body><h1>403 Forbidden</h1><p>Directory access is forbidden.</p></body></html>";
                    response = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(bodyContent.size()) + "\r\n\r\n" + bodyContent;
                }
            }
            else {
                // 资源不存在 -> 返回自定义 404 页面
                std::string escaped = html_escape(request.path);
                std::string bodyContent = "<html><head><meta charset='utf-8'></head><body><h1>404 Not Found</h1><p>Requested resource: " + escaped + " not found.</p></body></html>";
                response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(bodyContent.size()) + "\r\n\r\n" + bodyContent;
            }
        }
        // 5. 处理 POST 请求
        else if (request.method == "POST") {
            // 提取 Body
            size_t bodyPos = rawRequest.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                request.body = rawRequest.substr(bodyPos + 4);
            }

            // 解析参数 (此时 params 里的 string 还是 UTF-8 编码)
            auto params = parse_post_body(request.body);

            // --- 构建响应给浏览器 (保持 UTF-8) ---
            std::stringstream html;
            html << "<html><head><meta charset='utf-8'></head><body>"; // 确保浏览器知道是UTF-8
            html << "<h1>POST Received</h1>";
            html << "<table border='1'>";
            for (const auto& kv : params) {
                html << "<tr><td>" << kv.first << "</td><td>" << kv.second << "</td></tr>";
            }
            html << "</table></body></html>";
            std::string bodyContent = html.str();

            response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: text/html; charset=utf-8\r\n";
            response += "Content-Length: " + std::to_string(bodyContent.size()) + "\r\n\r\n";
            response += bodyContent;

            // --- 打印到控制台 (转为 ANSI/GBK 以避免乱码) ---
            std::cout << "  [POST Data Decoded]:" << std::endl;
            for (const auto& kv : params) {
                std::cout << "    " << Utf8ToAnsi(kv.first) << " = " << Utf8ToAnsi(kv.second) << std::endl;
            }
        }
        else {
            response = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
        }
    }
    catch (const std::exception& e) {
        // 任何文件系统异常都作为 403/500 处理，避免泄露内部路径信息
        response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
    }

    send(clientSocket, response.c_str(), (int)response.size(), 0);
    closesocket(clientSocket);
}

// ==========================================
// Main Entry Point
// ==========================================
int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    std::string rootDir = DEFAULT_ROOT;

    // 解析命令行参数
    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) rootDir = argv[2];

    // 规范化根目录为绝对并 canonical，方便后续比较和防止路径遍历
    try {
        fs::path rp = fs::absolute(rootDir);
        rp = fs::canonical(rp);
        rootDir = rp.string();
    }
    catch (...) {
        // 如果 canonical 失败（目录不存在），转为 absolute 但不要抛出异常，保持程序运行
        rootDir = fs::absolute(rootDir).string();
    }

    // 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    // 创建 Socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    // 绑定 & 监听
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server running on port " << port << std::endl;
    std::cout << "Serving files from: " << fs::absolute(rootDir).string() << std::endl;
    std::cout << "Test URL: http://localhost:" << port << "/" << std::endl;

    // 接受连接循环
    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            // 为每个客户端启动一个新线程
            std::thread(handle_client, clientSocket, rootDir).detach();
        }
    }

    // 清理资源 (虽然死循环不会运行到这里)
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}