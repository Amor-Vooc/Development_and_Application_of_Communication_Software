#include <winsock2.h>
#include <WS2tcpip.h>
#include <iostream>
#include <thread>
#include <memory>
#include <string>
#include <windows.h>  // 用于改变控制台颜色
#include <chrono>
//2023211281-丁同勖-Client

using namespace std;

#pragma comment(lib, "ws2_32.lib")

class ChatClient {
public:
    ChatClient(const string& ipAddress, int port);
    ~ChatClient();
    void Start();

private:
    static DWORD WINAPI RecvMessage(LPVOID lpThread);
    void SendUsername();
    void SendMessage();
    static void SetConsoleColor(const string& colorName);  // 设置控制台颜色为静态函数
    static void ResetConsoleColor();  // 重置控制台颜色为静态函数

    SOCKET socket_;
    bool running_;
};

// 新增控制/功能客户端
class ControlClient {
public:
    ControlClient(const string& ipAddress, int port);
    ~ControlClient();
    // 获取UDP端口并发送START
    int RequestUdpPort();
    string SendTimeRequest();
private:
    SOCKET ctrlSocket;
};

ChatClient::ChatClient(const string& ipAddress, int port) : running_(true) {
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        throw runtime_error("WSAStartup error: " + to_string(GetLastError()));
    }

    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        throw runtime_error("Socket error: " + to_string(GetLastError()));
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ipAddress.c_str(), &addr.sin_addr);

    if (connect(socket_, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        throw runtime_error("Connect error: " + to_string(GetLastError()));
    }

    // 发送用户名作为第一条消息
    SendUsername(); // 发送用户名

    // 启动接收线程
    HANDLE hThread = CreateThread(NULL, 0, RecvMessage, (LPVOID)socket_, 0, NULL);
    if (hThread == NULL) {
        throw runtime_error("创建接收线程失败: " + to_string(GetLastError()));
    }
    CloseHandle(hThread);
}

ChatClient::~ChatClient() {
    running_ = false; // 设置为 false，以便停止接收消息
    closesocket(socket_); // 关闭套接字
    WSACleanup(); // 清理 Winsock
}

void ChatClient::Start() {
    SendMessage(); // 发送聊天消息
}

void ChatClient::SendUsername() {
    std::string username;
    int space = 1;
    while (space) { // 检查用户名是否有空格
        std::cout << "请输入您的用户名: ";
        std::getline(std::cin, username);
        if (username.find(' ') == string::npos) {
            space = 0;
        }
        else {
            cout << "用户名不能含有空格，请重新输入！" << endl;
        }
    }

    // 发送用户名到服务器
    if (send(socket_, username.c_str(), username.size(), 0) == SOCKET_ERROR) {
        std::cout << "发送用户名失败: " << GetLastError() << std::endl;
        return;
    }

    // 接收服务器的确认消息
    char buf[100] = { 0 };
    recv(socket_, buf, sizeof(buf), 0);
    std::cout << "服务器确认: " << buf << std::endl;
    std::cout << "Tip：#颜色 [消息] 可以改变消息颜色。支持的颜色有红色、绿色、蓝色、黄色、洋红和青色。\n" << std::endl;
}

void ChatClient::SendMessage() {
    std::string message;
    while (running_) {
        cout << "请输入聊天内容：";
        std::getline(std::cin, message); // 使用 getline 来处理包含空格的输入

        if (!message.empty()) { // 确保不发送空消息

            size_t spacePos = string::npos;
            spacePos = message.find(' '); // 获取颜色后的空格位置
            if (message[spacePos + 1] != '\0') { // 检查含颜色的输入是否为空
                if (send(socket_, message.c_str(), message.size(), 0) == SOCKET_ERROR) {
                    cout << "发送消息失败: " << GetLastError() << endl;
                    break;
                }
                else {
                    cout << "消息发送成功！" << endl;
                }
            }
            else {
                cout << "消息不能为空，请重新输入。" << endl;
            }
        }
        else {
            cout << "消息不能为空，请重新输入。" << endl;
        }
    }
}

DWORD WINAPI ChatClient::RecvMessage(LPVOID lpThread) {
    SOCKET s = (SOCKET)lpThread;
    char buf[100] = { 0 };
    int ret = 0;

    while (true) {
        ret = recv(s, buf, sizeof(buf), 0);

        if (ret > 0) {
            string receivedMessage(buf, ret);

            // 清除未使用的输入提示并打印消息
            cout << "\033[1K\r";

            // 提取颜色和消息内容
            int count = 0;
            size_t spacePos1 = string::npos;
            size_t spacePos2 = string::npos;
            size_t end = string::npos;
            size_t startPos = 0;

            spacePos1 = receivedMessage.find(' ', startPos); // 获取用户名后的空格位置
            startPos = spacePos1 + 1; // 更新查找位置，防止重复查找

            spacePos2 = receivedMessage.find(' ', startPos); // 获取消息前的空格位置
            startPos = spacePos2 + 1; // 更新查找位置，防止重复查找

            end = receivedMessage.find('\0', startPos); // 获取消息结尾

            if (receivedMessage.size() > 5 && receivedMessage[spacePos1 + 5] == '#' && spacePos2 != string::npos) {
                string name = receivedMessage.substr(0, spacePos1 + 5); // 提取发送者用户名
                string colorCode = receivedMessage.substr(spacePos1 + 5, 5); // 识别颜色
                string msgContent = receivedMessage.substr(spacePos2 + 1, end - spacePos2 + 1); // 提取消息内容

                // 设置颜色
                SetConsoleColor(colorCode);
                cout << name;
                cout << msgContent << endl;

                // 重置颜色
                ResetConsoleColor();
            }
            else {
                cout << buf << endl;
            }

            cout << "请输入聊天内容："; // 提示用户输入

        }
        else {
            cout << "接收消息失败: " << GetLastError() << endl;
            break; // 连接关闭或出错
        }
    }
    return 0;
}

void ChatClient::SetConsoleColor(const string& colorCode) {
    // 根据颜色代码设置控制台颜色
    if (colorCode == "#红色") {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
    }
    else if (colorCode == "#绿色") {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }
    else if (colorCode == "#蓝色") {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
    else if (colorCode == "#黄色") {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }
    else if (colorCode == "#洋红") {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
    else if (colorCode == "#青色") {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
    else {
        // 默认颜色
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
}

void ChatClient::ResetConsoleColor() {
    // 重置为默认颜色
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

ControlClient::ControlClient(const string& ipAddress, int port) {
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        throw runtime_error("WSAStartup error: " + to_string(GetLastError()));
    }

    ctrlSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctrlSocket == INVALID_SOCKET) {
        throw runtime_error("Socket error: " + to_string(GetLastError()));
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ipAddress.c_str(), &addr.sin_addr);

    if (connect(ctrlSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        throw runtime_error("Connect error: " + to_string(GetLastError()));
    }

    // 表明控制连接
    send(ctrlSocket, "CTRL", 4, 0);
}

ControlClient::~ControlClient() {
    closesocket(ctrlSocket);
}

int ControlClient::RequestUdpPort() {
    char buf[256] = { 0 };
    int r = recv(ctrlSocket, buf, sizeof(buf) - 1, 0);
    if (r <= 0) return -1;
    string s(buf, r);
    // 格式 UDPPORT:<port>;START
    size_t p = s.find("UDPPORT:");
    if (p == string::npos) return -1;
    size_t semi = s.find(';', p);
    string num = s.substr(p + 8, semi - (p + 8));
    return stoi(num);
}

string ControlClient::SendTimeRequest() {
    send(ctrlSocket, "TIME", 4, 0);
    char buf[256] = { 0 };
    int r = recv(ctrlSocket, buf, sizeof(buf) - 1, 0);
    if (r <= 0) return string();
    return string(buf, r);
}

int main(int argc, char* argv[]) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

    string IP = "127.0.0.1";
    int PORT = 3000;

    if (argc == 1) {
        cout << "未设置指定 IP 和端口号，默认使用 127.0.0.1:3000" << endl;
    }
    else if (argc == 2) {
        cout << "IP : " << argv[1] << "，未设置指定端口号，默认使用 3000" << endl;
        IP = argv[1];
    }
    else if (argc == 3) {
        cout << "正在连接：" << argv[1] << ":" << argv[2] << endl;
        IP = argv[1];
        PORT = std::stoi(argv[2]); // 处理char* 转 int
    }

    try {
        // 控制连接获取 UDP 端口
        ControlClient ctrl(IP, PORT);
        int udpPort = ctrl.RequestUdpPort();
        cout << "服务器 UDP 端口: " << udpPort << endl;

        while (true) {
            cout << "菜单:\n1-Get current time (TCP)\n2-Echo Mode (UDP)\n3-Chat (TCP)\n4-Exit the program\n请输入选项: ";
            string opt;
            getline(cin, opt);
            if (opt == "1") {
                string t = ctrl.SendTimeRequest();
                cout << "服务器时间: " << t << endl;
            }
            else if (opt == "2") {
                // UDP echo: 创建UDP socket，向服务器发送消息并接收回显
                SOCKET u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (u == INVALID_SOCKET) {
                    cout << "创建UDP socket失败: " << GetLastError() << endl;
                    continue;
                }
                sockaddr_in serv;
                serv.sin_family = AF_INET;
                serv.sin_port = htons(udpPort);
                inet_pton(AF_INET, IP.c_str(), &serv.sin_addr);

                cout << "进入Echo模式，输入消息（输入 exit 返回菜单）：";
                string line;
                while (true) {
                    getline(cin, line);
                    if (line == "exit") break;
                    int sent = sendto(u, line.c_str(), (int)line.size(), 0, (SOCKADDR*)&serv, sizeof(serv));
                    if (sent == SOCKET_ERROR) {
                        cout << "发送失败: " << GetLastError() << endl;
                        break;
                    }
                    char buf[1024] = { 0 };
                    int fromlen = sizeof(serv);
                    int r = recvfrom(u, buf, sizeof(buf) - 1, 0, (SOCKADDR*)&serv, &fromlen);
                    if (r > 0) {
                        cout << "回显: " << string(buf, r) << endl;
                    }
                    cout << "输入消息（输入 exit 返回菜单）：";
                }
                closesocket(u);
            }
            else if (opt == "3") {
                // 进入聊天，建立一个新的 ChatClient 连接
                cout << "进入Chat，创建聊天连接..." << endl;
                // 使用现有PORT创建聊天连接
                ChatClient chat(IP, PORT);
                chat.Start();
            }
            else if (opt == "4") {
                cout << "退出程序" << endl;
                break;
            }
            else {
                cout << "无效选项" << endl;
            }
        }

    }
    catch (const exception& e) {
        cout << "错误: " << e.what() << endl;
    }
    return 0;
}
