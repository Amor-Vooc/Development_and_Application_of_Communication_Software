#include <ws2tcpip.h>
#include <winsock2.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>
#include <csignal>
#include <map>
#include <string>
#include <chrono>
#include <ctime>
//2023211281-丁同勖-Server

using namespace std;

#pragma comment(lib, "ws2_32.lib")

class ChatServer {
public:
    ChatServer(const string& IP, int port, int udpPort);
    void Start();
    void Stop();
    static void SignalHandler(int signal);
    static ChatServer* serverInstance;

private:
    void HandleTCPConnection(SOCKET clientSocket);
    void HandleChatClient(SOCKET clientSocket, const string& preReceivedUsername);
    void HandleControlClient(SOCKET clientSocket);
    void BroadcastMessage(const char* message, SOCKET senderSocket, int type);
    void PrintError(const string& message);
    void UdpListener();

    vector<SOCKET> clients; // 存储客户端套接字
    map<SOCKET, string> clientUsernames; // 存储客户端用户名
    mutex clientsMutex; // 保护对客户端的访问
    SOCKET serverSocket;
    SOCKET udpSocket;
    bool running = false;
    int accessCount = 0;
    int udpPort;
    string ip;
};

ChatServer* ChatServer::serverInstance = nullptr;

ChatServer::ChatServer(const string& IP, int port, int udpPort) : udpPort(udpPort), ip(IP) {
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        PrintError("WSAStartup失败");
        exit(1);
    }

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        PrintError("创建套接字失败");
        exit(1);
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, IP.c_str(), &addr.sin_addr);

    if (bind(serverSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        PrintError("绑定失败");
        exit(1);
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        PrintError("监听失败");
        exit(1);
    }

    // 创建并绑定UDP套接字
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        PrintError("创建UDP套接字失败");
        exit(1);
    }
    sockaddr_in udpAddr;
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port = htons(udpPort);
    inet_pton(AF_INET, IP.c_str(), &udpAddr.sin_addr);

    if (bind(udpSocket, (SOCKADDR*)&udpAddr, sizeof(udpAddr)) == SOCKET_ERROR) {
        PrintError("UDP绑定失败");
        closesocket(udpSocket);
        exit(1);
    }

    cout << "服务器正在监听 TCP 端口 " << port << " 和 UDP 端口 " << udpPort << "..." << endl;
    running = true;

    // 启动UDP监听线程
    thread(&ChatServer::UdpListener, this).detach();
}

void ChatServer::Start() {
    while (running) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            {
                lock_guard<mutex> lock(clientsMutex);
                accessCount++; // 增加访问计数
                cout << "当前访问次数: " << accessCount << endl;
            }
            thread(&ChatServer::HandleTCPConnection, this, clientSocket).detach();
        }
    }
}

void ChatServer::HandleTCPConnection(SOCKET clientSocket) {
    // 首先接收一段消息来判断连接类型（控制或聊天）。
    char buf[256] = {0};
    int ret = recv(clientSocket, buf, sizeof(buf) - 1, 0);
    if (ret <= 0) {
        closesocket(clientSocket);
        return;
    }

    string received(buf, ret);
    if (received == "CTRL") {
        HandleControlClient(clientSocket);
    } else {
        // 把接收到的数据当作用户名，继续聊天处理
        HandleChatClient(clientSocket, received);
    }
}

void ChatServer::HandleChatClient(SOCKET clientSocket, const string& preReceivedUsername) {
    string username = preReceivedUsername;
    if (username.empty()) {
        char usernameBuf[100] = {0};
        int r = recv(clientSocket, usernameBuf, sizeof(usernameBuf) - 1, 0);
        if (r <= 0) {
            closesocket(clientSocket);
            return;
        }
        username = string(usernameBuf, r);
    }

    {
        lock_guard<mutex> lock(clientsMutex);
        clients.push_back(clientSocket);
        clientUsernames[clientSocket] = username; // 存储用户名
    }

    cout << "欢迎 " << username << " 进入聊天室！" << endl;

    char welcomeMessage[200];
    snprintf(welcomeMessage, sizeof(welcomeMessage), "欢迎 %s 进入聊天室!", username.c_str());
    send(clientSocket, welcomeMessage, (int)strlen(welcomeMessage) + 1, 0);
    BroadcastMessage(welcomeMessage, clientSocket, 0);

    char bufMsg[200] = {0};
    int ret;

    do {
        memset(bufMsg, 0, sizeof(bufMsg));
        ret = recv(clientSocket, bufMsg, sizeof(bufMsg) - 1, 0);

        if (ret > 0) {
            cout << username << " 说：" << bufMsg << endl;
            BroadcastMessage(bufMsg, clientSocket, 1);
        }
    } while (ret > 0);

    {
        lock_guard<mutex> lock(clientsMutex);
        clients.erase(remove(clients.begin(), clients.end(), clientSocket), clients.end());
        clientUsernames.erase(clientSocket); // 移除用户名
    }

    cout << username << " 离开了聊天室" << endl;
    BroadcastMessage(username.c_str(), clientSocket, 2);
    closesocket(clientSocket);
}

void ChatServer::HandleControlClient(SOCKET clientSocket) {
    // 发送UDP端口和START命令
    char info[128];
    snprintf(info, sizeof(info), "UDPPORT:%d;START", udpPort);
    send(clientSocket, info, (int)strlen(info) + 1, 0);

    // 进入控制命令循环
    char buf[128];
    while (true) {
        memset(buf, 0, sizeof(buf));
        int ret = recv(clientSocket, buf, sizeof(buf) - 1, 0);
        if (ret <= 0) break;
        string cmd(buf, ret);
        if (cmd == "TIME") {
            // 发送当前时间
            auto now = chrono::system_clock::now();
            time_t t = chrono::system_clock::to_time_t(now);
            char timestr[64] = {0};
            if (ctime_s(timestr, sizeof(timestr), &t) == 0) {
                // ctime_s 返回带换行的字符串
                send(clientSocket, timestr, (int)strlen(timestr) + 1, 0);
				cout << "已发送时间给控制客户端: " << timestr << endl;
            } else {
                string err = "Failed to get time";
                send(clientSocket, err.c_str(), (int)err.size() + 1, 0);
				cout << "发送时间失败" << endl;
            }
        } else if (cmd == "EXIT") {
            break;
        } else {
            // 未知命令，回显
            string r = string("Unknown command: ") + cmd;
            send(clientSocket, r.c_str(), (int)r.size() + 1, 0);
			cout << "收到未知命令: " << cmd << endl;
        }
    }

    closesocket(clientSocket);
}

void ChatServer::UdpListener() {
    // 简单的回显服务器：接收到什么就发送回去
    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);
    char buf[1024];
    while (running) {
        memset(&fromAddr, 0, sizeof(fromAddr));
        memset(buf, 0, sizeof(buf));
        int r = recvfrom(udpSocket, buf, (int)sizeof(buf) - 1, 0, (SOCKADDR*)&fromAddr, &fromLen);
        if (r > 0) {
            // 回显
            sendto(udpSocket, buf, r, 0, (SOCKADDR*)&fromAddr, fromLen);
			cout << "UDP回显: " << string(buf, r) << endl;
        }
        else {
            this_thread::sleep_for(chrono::milliseconds(50));
        }
    }
}

void ChatServer::BroadcastMessage(const char* message, SOCKET senderSocket, int type) {
    lock_guard<mutex> lock(clientsMutex);
    for (const SOCKET& otherClient : clients) {
        if (otherClient != senderSocket) {
            char msg[200];
            if (type == 1) {
                snprintf(msg, sizeof(msg), "%s 说：%s", clientUsernames[senderSocket].c_str(), message);
            }
            else if (type == 2) {
                snprintf(msg, sizeof(msg), "服务器消息：%s 离开了聊天室", message);
            }
            else {
                snprintf(msg, sizeof(msg), "服务器消息：新成员加入！%s", message);
            }
            send(otherClient, msg, (int)strlen(msg) + 1, 0);
        }
    }
}

void ChatServer::PrintError(const string& message) {
    cout << message << ": " << GetLastError() << endl;
}

void ChatServer::Stop() {
    running = false; // 设置标志为false，退出主循环
    closesocket(serverSocket); // 关闭监听套接字
    closesocket(udpSocket);
    WSACleanup(); // 清理Winsock
    cout << "服务器已关闭。" << endl;
}

// 信号处理函数
void ChatServer::SignalHandler(int signal) {
    if (serverInstance) {
        serverInstance->Stop();
    }
}

int main(int argc, char* argv[]) {

    string IP = "127.0.0.1";
    int PORT = 3000;
    int UDPPORT = 4001; // 默认 UDP 端口，用户可以修改

    if (argc == 1) {
        cout << "未设置指定 IP 和端口号，默认使用 127.0.0.1:3000，UDP端口 " << UDPPORT << "" << endl;
    }
    else if (argc == 2) {
        cout << "IP : " << argv[1] << "，未设置指定端口号，默认使用 3000，UDP端口 " << UDPPORT << "" << endl;
        IP = argv[1];
    }
    else if (argc == 3) {
        cout << "正在连接：" << argv[1] << ":" << argv[2] << endl;
        IP = argv[1];
        PORT = std::stoi(argv[2]); // 处理char* 转 int
    }
    else if (argc >= 4) {
        IP = argv[1];
        PORT = std::stoi(argv[2]);
        UDPPORT = std::stoi(argv[3]);
        cout << "使用：" << IP << ":" << PORT << " UDP:" << UDPPORT << endl;
    }

    ChatServer server(IP, PORT, UDPPORT);
    ChatServer::serverInstance = &server;

    // 设置信号处理
    signal(SIGINT, ChatServer::SignalHandler);
    signal(SIGTERM, ChatServer::SignalHandler);

    server.Start();

    return 0;
}
