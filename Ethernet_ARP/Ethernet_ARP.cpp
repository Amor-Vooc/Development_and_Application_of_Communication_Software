#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include <cstring>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

// 防止结构体被自动填充，因为网络协议对字节布局有严格定义，不能有编译器自动插入的填充字节
#pragma pack(push, 1)

// 以太网帧头（14字节）
struct EthHeader {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
};

// ARP 包头（28字节）
struct ArpHeader {
    uint16_t hw_type;      // 硬件类型，1 表示以太网
    uint16_t proto_type;   // 协议类型，0x0800 表示 IPv4
    uint8_t hw_len;        // 硬件地址长度，Ethernet 为 6
    uint8_t proto_len;     // 协议地址长度，IPv4 为 4
    uint16_t opcode;       // 操作码：1=请求, 2=应答
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
};

#pragma pack(pop)

// 设置输出格式
void print_hex(const std::vector<uint8_t>& buf) {
    for (size_t i = 0; i < buf.size(); ++i) {
        if (i % 16 == 0) std::cout << std::endl;
        std::cout << std::hex << std::setw(2)
            << std::setfill('0') << (int)buf[i] << ' ';
    }
    std::cout << std::dec << std::endl;
}

// 将字符串IP转成4字节
void ipv4_from_str(const char* s, uint8_t out[4]) {
    unsigned int a, b, c, d;
    sscanf_s(s, "%u.%u.%u.%u", &a, &b, &c, &d);
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
}

// 构造 ARP 请求包
std::vector<uint8_t> build_arp_request(
    const uint8_t src_mac[6],
    const char* src_ip_str,
    const uint8_t dst_mac_broadcast[6],
    const char* target_ip_str
) {
    std::vector<uint8_t> buf(sizeof(EthHeader) + sizeof(ArpHeader));
    EthHeader eth{};
    ArpHeader arp{};

    // Ethernet 头
    memcpy(eth.dst_mac, dst_mac_broadcast, 6);
    memcpy(eth.src_mac, src_mac, 6);
    eth.ethertype = htons(0x0806); // ARP

    // ARP 头
    arp.hw_type = htons(1);         // Ethernet
    arp.proto_type = htons(0x0800); // IPv4
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = htons(1);          // request

    memcpy(arp.sender_mac, src_mac, 6);
    ipv4_from_str(src_ip_str, arp.sender_ip);
    memset(arp.target_mac, 0x00, 6);
    ipv4_from_str(target_ip_str, arp.target_ip);

    size_t offset = 0;
    memcpy(buf.data() + offset, &eth, sizeof(eth));
    offset += sizeof(eth);
    memcpy(buf.data() + offset, &arp, sizeof(arp));

    return buf;
}

// 构造 ARP 应答包
std::vector<uint8_t> build_arp_reply(
    const uint8_t src_mac[6],
    const char* src_ip_str,
    const uint8_t dst_mac[6],
    const char* target_ip_str
) {
    std::vector<uint8_t> buf(sizeof(EthHeader) + sizeof(ArpHeader));
    EthHeader eth{};
    ArpHeader arp{};

    memcpy(eth.dst_mac, dst_mac, 6);
    memcpy(eth.src_mac, src_mac, 6);
    eth.ethertype = htons(0x0806);

    arp.hw_type = htons(1);
    arp.proto_type = htons(0x0800);
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = htons(2);

    memcpy(arp.sender_mac, src_mac, 6);
    ipv4_from_str(src_ip_str, arp.sender_ip);

    memcpy(arp.target_mac, dst_mac, 6);
    ipv4_from_str(target_ip_str, arp.target_ip);

    size_t offset = 0;
    memcpy(buf.data() + offset, &eth, sizeof(eth));
    offset += sizeof(eth);
    memcpy(buf.data() + offset, &arp, sizeof(arp));

    return buf;
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    uint8_t my_mac[6] = { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc };
    uint8_t broadcast_mac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t other_mac[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };

    // 构造请求包：192.168.1.10 向 192.168.1.100 查询
    auto request = build_arp_request(my_mac, "192.168.1.10", broadcast_mac, "192.168.1.100");
    std::cout << "ARP Request (" << request.size() << " bytes):";
    print_hex(request);

    // 构造应答包：192.168.1.100 告诉 192.168.1.10 它的MAC
    auto reply = build_arp_reply(my_mac, "192.168.1.100", other_mac, "192.168.1.10");
    std::cout << "\nARP Reply (" << reply.size() << " bytes):";
    print_hex(reply);

    WSACleanup();
    return 0;
}
