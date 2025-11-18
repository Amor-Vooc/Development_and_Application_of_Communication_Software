// 禁用一些旧版函数API的安全警告，以便于使用一些传统的Windows Socket函数
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

// -------------------------------
// 包含头文件
// -------------------------------
#include <winsock2.h>     // Windows Socket 2 主要头文件
#include <ws2tcpip.h>     // TCP/IP 协议相关的附加定义，如 inet_pton
#include <iphlpapi.h>    // IP 帮助程序 API，用于获取网络适配器信息
#include <mstcpip.h>      // 包含 SIO_RCVALL 等 IOCTL 控制码的定义
#include <iostream>       // 标准输入输出流
#include <iomanip>        // 用于格式化输出，如 setw
#include <map>            // 用于存储统计数据
#include <vector>         // 用于存储网络适配器列表
#include <string>         // C++ 字符串操作
#include <thread>         // C++11 线程支持，用于实时显示
#include <chrono>         // C++11 时间库，用于计时

// -------------------------------
// 链接库
// -------------------------------
#pragma comment(lib, "ws2_32.lib")      // 链接 Windows Socket 2 库
#pragma comment(lib, "iphlpapi.lib")    // 链接 IP 帮助程序 API 库

// -------------------------------
// 全局常量与命名空间
// -------------------------------
// 定义带有颜色的控制台输出消息前缀，以区分不同类型的消息
const std::string ErrorMsg = "\033[31m[ERR]\033[0m\t";      // 红色错误消息
const std::string InformationMsg = "\033[32m[INFO]\033[0m\t"; // 绿色信息消息
const std::string WarningMsg = "\033[33m[WARN]\033[0m\t";    // 黄色警告消息

using namespace std;

// -------------------------------
// 数据结构定义
// -------------------------------

/**
 * @brief 用于作为 map 的键，以统计不同的数据流。
 *        一个数据流由源IP、目的IP和协议唯一确定。
 */
struct Key {
    string src;   // 源IP地址
    string dst;   // 目的IP地址
    string proto; // 协议名称

    /**
     * @brief 重载小于运算符，使得 Key 结构体可以被用作 std::map 的键。
     * @param other 另一个 Key 对象。
     * @return 如果当前对象小于 other，则返回 true。
     */
    bool operator<(const Key& other) const {
        return std::tie(src, dst, proto) < std::tie(other.src, other.dst, other.proto);
    }
};

/**
 * @brief 将IP头中的协议号转换为可读的字符串名称。
 * @param proto IP数据包头中的协议字段值 (1 byte)。
 * @return 对应的协议名称字符串。
 */
string protocolName(unsigned char proto) {
    switch (proto) {
    case 1: return "ICMP";
    case 2: return "IGMP";
    case 6: return "TCP";
    case 17: return "UDP";
    default: return "Other"; // 其他未识别的协议
    }
}

// -------------------------------
// 主函数
// -------------------------------
int main(int argc, char* argv[])
{
    // --- 1. 参数检查 ---
    if (argc != 2) {
        cout << ErrorMsg << "用法: IP_Monitor.exe <抓包时间(秒)>\n";
        return -1;
    }

    int captureSeconds = atoi(argv[1]); // 将命令行参数从字符串转换为整数
    if (captureSeconds <= 0) {
        cout << ErrorMsg << "抓包时间无效\n";
        return -1;
    }

    // --- 2. 初始化 Winsock ---
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cout << ErrorMsg << "WSAStartup 初始化失败\n";
        return -1;
    }

    // --- 3. 枚举并选择网络适配器 ---
    ULONG len = 15000; // 预分配一个足够大的缓冲区
    IP_ADAPTER_INFO* pAdapterInfo = (IP_ADAPTER_INFO*)malloc(len);

    // 第一次调用 GetAdaptersInfo 获取所需的缓冲区大小
    if (GetAdaptersInfo(pAdapterInfo, &len) == ERROR_BUFFER_OVERFLOW) {
        free(pAdapterInfo); // 释放旧缓冲区
        pAdapterInfo = (IP_ADAPTER_INFO*)malloc(len); // 按实际大小重新分配
    }
    // 第二次调用获取信息
    if (GetAdaptersInfo(pAdapterInfo, &len) != NO_ERROR) {
        cout << ErrorMsg << "GetAdaptersInfo() 失败\n";
        free(pAdapterInfo);
        return -1;
    }

    vector<IP_ADAPTER_INFO*> adapters; // 存储所有适配器信息的指针
    IP_ADAPTER_INFO* p = pAdapterInfo;

    cout << InformationMsg << "本机网卡列表：\n";
    int idx = 1;
    int maxDescriptionLen = 0; // 用于对齐输出

    // 遍历链表，找到最长的描述长度
    while (p) {
        int currentLen = strlen(p->Description);
        if (currentLen > maxDescriptionLen)
            maxDescriptionLen = currentLen;
        adapters.push_back(p);
        p = p->Next;
    }

    // 打印所有适配器信息
    for (auto* adp : adapters) {
        cout << "\t" << "\033[33m" << idx++ << ".\033[0m " << left << setw(maxDescriptionLen + 2) << adp->Description
            << "\033[33mIP: \033[0m" << adp->IpAddressList.IpAddress.String << "\n";
    }

    // 获取用户选择
    cout << "\n请输入网卡编号：";
    int choice;
    cin >> choice;

    if (choice <= 0 || choice > adapters.size()) {
        cout << ErrorMsg << "无效的网卡编号\n";
        free(pAdapterInfo);
        return -1;
    }

    // 保存用户选择的网卡信息和IP地址
    string AdapterInfo = adapters[choice - 1]->Description;
    string localIP = adapters[choice - 1]->IpAddressList.IpAddress.String;

    // 释放为适配器信息分配的内存
    free(pAdapterInfo);

    // --- 4. 创建原始套接字并绑定 ---
    // AF_INET: IPv4, SOCK_RAW: 原始套接字, IPPROTO_IP: 捕获IP层数据包
    SOCKET s = socket(AF_INET, SOCK_RAW, IPPROTO_IP);
    if (s == INVALID_SOCKET) {
        cout << ErrorMsg << "socket 创建失败，错误代码: " << WSAGetLastError() << "\n";
        WSACleanup();
        return -1;
    }

    // 绑定套接字到选择的网卡IP上
    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    inet_pton(AF_INET, localIP.c_str(), &localAddr.sin_addr);
    localAddr.sin_port = 0; // 端口号对于原始套接字无意义

    if (bind(s, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        cout << ErrorMsg << "bind 失败（需要以管理员权限运行），错误代码: " << WSAGetLastError() << "\n";
        closesocket(s);
        WSACleanup();
        return -1;
    }

    // --- 5. 开启网卡混杂模式 ---
    // SIO_RCVALL: 控制码，使网卡接收所有流经它的数据包，而不仅仅是发给本机的数据包
    DWORD dwValue = 1; // 1 表示开启
    if (WSAIoctl(s, SIO_RCVALL, &dwValue, sizeof(dwValue),
        NULL, 0, &dwValue, NULL, NULL) == SOCKET_ERROR) {
        cout << ErrorMsg << "无法开启混杂模式，请确保以管理员权限运行！错误代码: " << WSAGetLastError() << "\n";
        closesocket(s);
        WSACleanup();
        return -1;
    }

    // --- 6. 准备数据统计与多线程显示 ---
    map<Key, int> statistics; // 用于存储数据包统计结果
    bool running = true; // 控制显示线程的循环

    auto startTime = chrono::steady_clock::now(); // 记录抓包开始时间
    auto endTime = startTime + chrono::seconds(captureSeconds); // 计算抓包结束时间

    // 创建一个子线程，用于实时刷新和显示统计数据
    thread displayThread([&]() {
        while (running) {
            system("cls"); // 清空控制台屏幕

            // 打印标题和当前状态
            cout << InformationMsg << "\033[33m当前选择网卡信息：\033[0m" << AdapterInfo << "\033[33m IP: \033[0m" << localIP << endl;
            auto elapsed = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - startTime).count() + 1;
            if (elapsed > captureSeconds) {
                cout << InformationMsg << "开始抓包 " << captureSeconds << "/" << captureSeconds << " 秒...\n";
            }
            else {
                cout << InformationMsg << "开始抓包 " << elapsed << "/" << captureSeconds << " 秒...\n";
            }
            cout << InformationMsg << "实时 IP 数据包统计（每秒刷新）\n";
            cout << "\033[32m-----------------------------------------------------------\033[0m\n";
            cout << "\033[33m" << left << setw(18) << "源IP"
                << setw(18) << "目的IP"
                << setw(10) << "协议"
                << setw(8) << "数量" << "\033[0m" << endl;
            cout << "\033[32m-----------------------------------------------------------\033[0m\n";

            // 遍历 map 并打印统计数据
            for (auto& kv : statistics) {
                cout << left << setw(18) << kv.first.src
                    << setw(18) << kv.first.dst
                    << setw(10) << kv.first.proto
                    << setw(8) << kv.second << endl;
            }

            cout << "\033[32m-----------------------------------------------------------\033[0m\n";
            this_thread::sleep_for(chrono::seconds(1)); // 每秒刷新一次
        }
        });

    // --- 7. 主循环：抓包与分析 ---
    unsigned char buffer[65536]; // 定义一个足够大的缓冲区来接收数据包

    while (chrono::steady_clock::now() < endTime) {
        // 从套接字接收数据，recv会阻塞直到有数据到达
        int ret = recv(s, (char*)buffer, sizeof(buffer), 0);
        if (ret <= 0) continue; // 如果接收失败或没有数据，则继续下一次循环

        // ---- 解析IP头 ----
        // 接收到的 buffer 直接就是 IP 头开始的数据
        unsigned char* ip = buffer;

        // IP头第10个字节(偏移量为9)是协议字段
        unsigned char proto = ip[9];

        // 解析源和目的IP地址
        // 源IP地址在偏移量 12-15 字节
        // 目的IP地址在偏移量 16-19 字节
        char srcIP[16], dstIP[16];
        sprintf(srcIP, "%d.%d.%d.%d", ip[12], ip[13], ip[14], ip[15]);
        sprintf(dstIP, "%d.%d.%d.%d", ip[16], ip[17], ip[18], ip[19]);

        // ---- 数据包过滤 ----
        // 过滤掉广播包
        if (strcmp(dstIP, "255.255.255.255") == 0)
            continue;

        // 只统计与本机IP相关的数据包（本机作为源或目的）
        if (localIP != srcIP && localIP != dstIP)
            continue;

        // ---- 更新统计信息 ----
        Key k{ srcIP, dstIP, protocolName(proto) }; // 创建一个Key
        statistics[k]++; // 更新对应数据流的计数
    }

    // --- 8. 结束与清理 ---
    running = false; // 通知显示线程退出循环
    displayThread.join(); // 等待显示线程执行完毕

    // 关闭套接字并清理 Winsock 环境
    closesocket(s);
    WSACleanup();

    cout << "\n" << InformationMsg << "抓包结束！\n";

    return 0;
}