#define _WINSOCK_DEPRECATED_NO_WARNINGS // 允许使用 inet_addr, gethostbyname 等旧函数

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
//#include <iomanip>

// 链接 Winsock 库
#pragma comment(lib, "ws2_32.lib")

using namespace std;

// ============================================================================
// 常量定义
// ============================================================================
#define ICMP_ECHO_REQUEST   8
#define ICMP_ECHO_REPLY     0
#define ICMP_TIMEOUT        11
#define DEF_ICMP_DATA_SIZE  32
#define MAX_HOPS            30
#define DEF_TIMEOUT         3000 // 默认超时时间 3000ms (3秒)

// ============================================================================
// 数据结构定义
// ============================================================================

// IP 报头结构
typedef struct {
    unsigned char  h_len : 4;        // 首部长度
    unsigned char  version : 4;      // 版本
    unsigned char  tos;              // 服务类型
    unsigned short total_len;        // 总长度
    unsigned short ident;            // 标识
    unsigned short frag_and_flags;   // 标志与片偏移
    unsigned char  ttl;              // 生存时间
    unsigned char  proto;            // 协议
    unsigned short checksum;         // 校验和
    unsigned int   sourceIP;         // 源IP地址
    unsigned int   destIP;           // 目的IP地址
} IpHeader;

// ICMP 报头结构
typedef struct {
    unsigned char  i_type;           // 类型
    unsigned char  i_code;           // 代码
    unsigned short i_cksum;          // 校验和
    unsigned short i_id;             // 标识符
    unsigned short i_seq;            // 序列号
    unsigned int   timestamp;        // 数据部分：简单的时间戳
} IcmpHeader;

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * 计算校验和
 * @param buffer 数据缓冲区
 * @param size 数据大小
 * @return 计算出的校验和
 */
unsigned short checksum(unsigned short* buffer, int size) {
    unsigned long cksum = 0;
    while (size > 1) {
        cksum += *buffer++;
        size -= sizeof(unsigned short);
    }
    if (size) {
        cksum += *(unsigned char*)buffer;
    }
    // 将32位累加和折叠成16位
    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >> 16);
    return (unsigned short)(~cksum);
}

// ============================================================================
// 主程序
// ============================================================================

int main(int argc, char* argv[]) {
    // 0. 参数校验
    if (argc != 2) {
        cout << "Usage: itracert.exe ip_or_hostname" << endl;
        return 1;
    }

    // 1. 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed." << endl;
        return 1;
    }

    // 2. 解析目的地址
    char* destStr = argv[1];
    unsigned long destIp = inet_addr(destStr);
    struct hostent* remoteHost;

    // 如果输入的是域名而非 IP，则进行域名解析
    if (destIp == INADDR_NONE) {
        remoteHost = gethostbyname(destStr);
        if (remoteHost == NULL) {
            cerr << "无法解析主机名: " << destStr << endl;
            WSACleanup();
            return 1;
        }
        destIp = *(unsigned long*)remoteHost->h_addr_list[0];
    }

    // 转换 IP 为字符串形式以便显示
    struct in_addr destAddrStruct;
    destAddrStruct.s_addr = destIp;
    char* destIpStr = inet_ntoa(destAddrStruct);

    // 3. 创建原始套接字 (Raw Socket)
    // 注意：创建原始套接字通常需要管理员权限
    SOCKET sockRaw = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockRaw == INVALID_SOCKET) {
        cerr << "无法创建套接字。请确认是否以【管理员权限】运行程序。" << endl;
        cerr << "Error Code: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    // 设置 Socket 接收超时
    int timeout = DEF_TIMEOUT;
    setsockopt(sockRaw, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    // 4. 准备目的地址结构
    sockaddr_in destSockAddr;
    memset(&destSockAddr, 0, sizeof(destSockAddr));
    destSockAddr.sin_family = AF_INET;
    destSockAddr.sin_addr.s_addr = destIp;

    // 5. 输出头部信息
    cout << "==== 开始跟踪“" << destStr << "”（最大 " << MAX_HOPS << " 跳）====" << endl;
    cout << "跳数 \t往返时间 (ms) \t节点IP地址" << endl;

    // 6. 主循环：TTL 从 1 到 MAX_HOPS
    bool reachedDest = false;
    USHORT seq_no = 0;
    int processId = GetCurrentProcessId(); // 使用进程ID作为 ICMP ID

    for (int ttl = 1; ttl <= MAX_HOPS; ++ttl) {
        // 6.1 设置 IP 头部的 TTL 字段
        if (setsockopt(sockRaw, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof(ttl)) == SOCKET_ERROR) {
            cerr << "Set TTL failed." << endl;
            break;
        }

        // 6.2 构造 ICMP 报文
        char icmp_data[sizeof(IcmpHeader) + DEF_ICMP_DATA_SIZE];
        IcmpHeader* icmp_hdr = (IcmpHeader*)icmp_data;
        memset(icmp_data, 0, sizeof(icmp_data));

        icmp_hdr->i_type = ICMP_ECHO_REQUEST;
        icmp_hdr->i_code = 0;
        icmp_hdr->i_id = (unsigned short)processId;
        icmp_hdr->i_seq = ++seq_no;
        icmp_hdr->i_cksum = 0;
        // 计算校验和必须在填充完所有数据后进行
        icmp_hdr->i_cksum = checksum((unsigned short*)icmp_data, sizeof(icmp_data));

        // 记录发送时间
        DWORD startTime = GetTickCount();

        // 6.3 发送 ICMP 请求
        int iResult = sendto(sockRaw, icmp_data, sizeof(icmp_data), 0, (sockaddr*)&destSockAddr, sizeof(destSockAddr));
        if (iResult == SOCKET_ERROR) {
            cout << ttl << "\t*\t\t目标不可达(Send Fail)" << endl;
            continue;
        }

        // 6.4 接收响应
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        char recvBuf[1024];

        // 使用 select 模型处理超时
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockRaw, &readfds);

        struct timeval timeVal;
        timeVal.tv_sec = DEF_TIMEOUT / 1000; // 3秒超时
        timeVal.tv_usec = 0;

        int selectResult = select(0, &readfds, NULL, NULL, &timeVal);

        if (selectResult > 0) {
            // 有数据可读
            int recvLen = recvfrom(sockRaw, recvBuf, sizeof(recvBuf), 0, (sockaddr*)&fromAddr, &fromLen);
            if (recvLen == SOCKET_ERROR) {
                cout << ttl << "\t*\t\t目标不可达 (Recv Error)" << endl;
            }
            else {
                // 收到数据，解析 IP 头和 ICMP 头
                DWORD endTime = GetTickCount();
                DWORD rtt = endTime - startTime;

                // 解析接收到的 IP 头
                IpHeader* recvIpHdr = (IpHeader*)recvBuf;
                int ipHdrLen = recvIpHdr->h_len * 4; // IP头长度单位是4字节

                // 确保接收到的长度足够包含 IP头 + ICMP头
                if (recvLen >= ipHdrLen + (int)sizeof(IcmpHeader)) {
                    // 定位到 ICMP 头部
                    IcmpHeader* recvIcmpHdr = (IcmpHeader*)(recvBuf + ipHdrLen);

                    // 处理不同类型的 ICMP 响应
                    if (recvIcmpHdr->i_type == ICMP_TIMEOUT) {
                        // 类型 11: TTL 超时（这是路径上的中间路由器）
                        cout << ttl << "\t";
                        if (rtt < 1) cout << "<1ms";
                        else cout << rtt << "ms";
                        cout << "\t\t" << inet_ntoa(fromAddr.sin_addr) << endl;

                    }
                    else if (recvIcmpHdr->i_type == ICMP_ECHO_REPLY) {
                        // 类型 0: 回显应答（到达目的地）
                        // 检查 ID 是否匹配（确认是本进程发的包）
                        if (recvIcmpHdr->i_id == (unsigned short)processId) {
                            cout << ttl << "\t";
                            if (rtt < 1) cout << "<1ms";
                            else cout << rtt << "ms";
                            cout << "\t\t" << inet_ntoa(fromAddr.sin_addr) << endl;
                            reachedDest = true;
                        }
                        else {
                            // ID 不匹配，可能是其他程序的包或旧包
                            cout << ttl << "\t*\t\t目标不可达 (ID mismatch)" << endl;
                        }

                    }
                    else {
                        // 其他类型，如类型 3 (目标不可达)
                        cout << ttl << "\t";
                        if (rtt < 1) cout << "<1ms";
                        else cout << rtt << "ms";
                        cout << "\t\t" << inet_ntoa(fromAddr.sin_addr)
                            << " (Type " << (int)recvIcmpHdr->i_type << ")" << endl;
                    }
                }
            }
        }
        else {
            // select 返回 0 (超时) 或 < 0 (错误)
            cout << ttl << "\t*\t\t请求超时" << endl;
        }

        if (reachedDest) break;
    }

    // 7. 结束程序
    cout << "本次跟踪完成，请按任意键结束程序" << endl;

    closesocket(sockRaw);
    WSACleanup();

    cin.get(); // 等待用户按键
    return 0;
}