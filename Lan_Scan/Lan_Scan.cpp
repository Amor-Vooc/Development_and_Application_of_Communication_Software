// 在某些较新的 Visual Studio 版本中，一些传统的 Winsock 函数被标记为“已弃用”，定义此宏以禁用关于使用旧版 Winsock 函数的警告。
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>     // Winsock 核心功能，用于网络编程
#include <iphlpapi.h>     // IP Helper API，用于获取网络适配器信息，如 GetAdaptersInfo, SendARP
#include <ws2tcpip.h>     // Winsock 2 TCP/IP 协议的附加定义
#include <windows.h>      // Windows 核心 API，包含基本数据类型和函数
#include <iostream>       // 用于标准输入输出流 (cout, cerr)
#include <iomanip>        // 用于流操作，如 setfill, setw
#include <string>         // C++ 字符串类 (std::string)
#include <vector>         // C++ 动态数组 (std::vector)
#include <cstdint>        // 标准整数类型，如 uint32_t
#include <sstream>        // 字符串流，用于构建字符串
#include <thread>         // C++11 线程支持 (std::thread)
#include <atomic>         // C++11 原子操作，用于线程安全计数器
#include <mutex>          // C++11 互斥锁，用于保护共享资源

#pragma comment(lib, "iphlpapi.lib") // 链接 IP Helper API 库，提供网络管理功能
#pragma comment(lib, "ws2_32.lib")   // 链接 Winsock 2.2 库，提供核心网络套接字功能

const std::string ErrorMsg = "\033[31m[ERR]\033[0m\t";
const std::string InformationMsg = "\033[32m[INFO]\033[0m\t";
const std::string WarningMsg = "\033[33m[WARN]\033[0m\t";

using namespace std;

/**
 * @brief 将字节数组格式化为冒号分隔的十六进制 MAC 地址字符串。
 * @param addr 指向包含 MAC 地址的字节数组的指针。
 * @param len MAC 地址的长度（以字节为单位）。
 * @return 格式化后的 MAC 地址字符串。如果长度为 0，则返回空字符串。
 */
static string formatMac(const BYTE* addr, ULONG len) {
    if (len == 0) return "";
    ostringstream oss;
    oss << hex << setfill('0'); // 设置为十六进制输出，并用 '0' 填充
    for (ULONG i = 0; i < len; ++i) {
        if (i) oss << ':'; // 在字节之间添加冒号
        oss << setw(2) << static_cast<int>(addr[i]); // 格式化每个字节为两位十六进制数
    }
    return oss.str();
}

/**
 * @brief 检查给定的 MAC 地址是否为全零。
 * @param addr 指向包含 MAC 地址的字节数组的指针。
 * @param len MAC 地址的长度（以字节为单位）。
 * @return 如果 MAC 地址全为零，则返回 true，否则返回 false。
 */
static bool isZeroMac(const BYTE* addr, ULONG len) {
    for (ULONG i = 0; i < len; ++i)
        if (addr[i] != 0) return false; // 一旦发现非零字节，立即返回 false
    return true;
}

int main() {
    // 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << ErrorMsg << "WSAStartup failed" << endl;
        return 1;
    }

    // --- 获取网络适配器信息 ---
    ULONG outBufLen = 0;
    // 第一次调用 GetAdaptersInfo，传入 nullptr，以获取所需的缓冲区大小
    DWORD dwRet = GetAdaptersInfo(nullptr, &outBufLen);
    if (dwRet != ERROR_BUFFER_OVERFLOW) {
        cerr << ErrorMsg << "GetAdaptersInfo failed to get buffer size: " << dwRet << endl;
        WSACleanup();
        return 1;
    }

    // 根据获取的大小分配缓冲区
    vector<BYTE> buffer(outBufLen);
    PIP_ADAPTER_INFO pAdapterInfo = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());

    // 第二次调用 GetAdaptersInfo，传入分配好的缓冲区以获取适配器信息
    dwRet = GetAdaptersInfo(pAdapterInfo, &outBufLen);
    if (dwRet != NO_ERROR) {
        cerr << ErrorMsg << "GetAdaptersInfo failed: " << dwRet << endl;
        WSACleanup();
        return 1;
    }

    cout << InformationMsg << "网络适配器与扫描结果：" << endl;
   
    // --- 遍历所有网络适配器 ---
    for (PIP_ADAPTER_INFO pAdapter = pAdapterInfo; pAdapter != nullptr; pAdapter = pAdapter->Next) {
        cout << endl << InformationMsg << "\033[32m--------------------------[适配器信息]--------------------------\033[0m\t" << endl;
        cout << InformationMsg << "\033[33m适配器:\033[0m" << pAdapter->AdapterName << " (" << pAdapter->Description << ")" << endl;
        cout << InformationMsg << "\033[33mMAC: \033[0m" << formatMac(pAdapter->Address, pAdapter->AddressLength) << endl;

        // --- 遍历适配器的每个 IP 地址 ---
        for (IP_ADDR_STRING* ipAddr = &pAdapter->IpAddressList; ipAddr != nullptr; ipAddr = ipAddr->Next) {
            if (ipAddr->IpAddress.String[0] == '\0') break; // 如果 IP 地址字符串为空，则停止
            string ipStr = ipAddr->IpAddress.String;
            string maskStr = ipAddr->IpMask.String;

            // 忽略无效的 "0.0.0.0" 地址
            if (ipStr == "0.0.0.0") continue;

            cout << InformationMsg << "\033[33mIP: \033[0m" << ipStr << "\033[33m 掩码: \033[0m" << maskStr << "\033[33m 网关: \033[0m" << pAdapter->GatewayList.IpAddress.String << endl;
			cout << InformationMsg << "\033[32m[开始扫描局域网...]\033[0m\t" << endl;

            // --- 计算子网范围 ---
            uint32_t ip = ntohl(inet_addr(ipStr.c_str()));   // 将点分十进制 IP 字符串转为网络字节序整数，再转为主机字节序
            uint32_t mask = ntohl(inet_addr(maskStr.c_str())); // 同上处理子网掩码
            if (mask == 0) continue; // 如果掩码无效，则跳过

            uint32_t network = ip & mask;                     // 计算网络地址
            uint32_t broadcast = network | (~mask);           // 计算广播地址

            // 打印子网范围
            cout << InformationMsg << "\033[33m扫描子网范围: \033[0m" << ((network >> 24) & 0xFF) << "." << ((network >> 16) & 0xFF) << "." << ((network >> 8) & 0xFF) << "." << (network & 0xFF)
                << "\033[33m - \033[0m" << ((broadcast >> 24) & 0xFF) << "." << ((broadcast >> 16) & 0xFF) << "." << ((broadcast >> 8) & 0xFF) << "." << (broadcast & 0xFF) << endl;

            // --- 准备要扫描的主机地址 ---
            uint32_t start = network + 1; // 子网的第一个可用主机地址
            uint32_t end = (broadcast > 0) ? (broadcast - 1) : broadcast; // 子网的最后一个可用主机地址
            uint64_t hostCount = (end >= start) ? (uint64_t)(end - start + 1) : 0;

            if (hostCount == 0) {
                cout << WarningMsg << "No hosts to scan on this interface." << endl;
                continue;
            }
            // 避免扫描过大的子网（例如 /16），这会消耗大量时间和资源
            if (hostCount > 65534) {
                cout << WarningMsg << "Subnet too large (" << hostCount << " hosts). Skipping detailed scan." << endl;
                continue;
            }

            // 创建要扫描的 IP 地址列表（网络字节序）
            vector<uint32_t> candidates;
            candidates.reserve(static_cast<size_t>(hostCount));
            for (uint32_t h = start; h <= end; ++h) {
                candidates.push_back(htonl(h)); // 存入网络字节序，因为 SendARP 需要这种格式
            }

            // --- 多线程扫描 ---
            // 确定要使用的线程数
            unsigned int hc = thread::hardware_concurrency(); // 获取硬件支持的并发线程数
            if (hc == 0) hc = 4; // 如果无法获取，则默认为 4
            unsigned int maxThreads = hc;
            if (maxThreads > 64) maxThreads = 64; // 限制最大线程数为 64
            if (maxThreads > candidates.size()) maxThreads = static_cast<unsigned int>(candidates.size()); // 线程数不超过 IP 数量
            if (maxThreads == 0) maxThreads = 1; // 至少使用一个线程

            cout << InformationMsg << "使用 " << maxThreads << " 个线程扫描 " << candidates.size() << " 台主机..." << endl;

            atomic<size_t> nextIndex(0); // 原子计数器，用于线程安全地分配任务
            mutex printMutex;            // 互斥锁，用于保护 cout 输出，防止多线程同时写入导致混乱

            // 工作线程函数
            auto worker = [&](unsigned int /*threadId*/) {
                for (;;) {
                    // 原子地获取并增加索引，确保每个 IP 只被一个线程处理
                    size_t idx = nextIndex.fetch_add(1);
                    if (idx >= candidates.size()) break; // 如果所有 IP 都已分配，则线程退出

                    uint32_t candidate = candidates[idx]; // 获取要扫描的 IP 地址
                    BYTE macAddr[8] = { 0 }; // 存储 MAC 地址的缓冲区
                    ULONG macAddrLen = sizeof(macAddr);

                    // 发送 ARP 请求
                    DWORD arpRet = SendARP((IPAddr)candidate, 0, macAddr, &macAddrLen);

                    // 如果 ARP 请求成功，且返回了有效的、非零的 MAC 地址
                    if (arpRet == NO_ERROR && macAddrLen > 0 && !isZeroMac(macAddr, macAddrLen)) {
                        in_addr a;
                        a.S_un.S_addr = candidate;
                        const char* ipBuf = inet_ntoa(a); // 将 IP 地址转回字符串格式
                        string macs = formatMac(macAddr, macAddrLen); // 格式化 MAC 地址

                        // 使用互斥锁保护控制台输出
                        lock_guard<mutex> lk(printMutex);
                        cout << InformationMsg << "\033[33mIP: \033[0m" << ipBuf << "\033[33m ---> \033[0m" << "\033[33mMAC: \033[0m" << macs << endl;
                    }
                }
                };

            // 创建并启动线程
            vector<thread> threads;
            threads.reserve(maxThreads);
            for (unsigned int t = 0; t < maxThreads; ++t) {
                threads.emplace_back(worker, t);
            }
            // 等待所有线程完成
            for (auto& th : threads) {
                th.join();
            }
        } // 遍历 IP 地址结束
    } // 遍历适配器结束

    // 清理 Winsock
    WSACleanup();
    return 0;
}