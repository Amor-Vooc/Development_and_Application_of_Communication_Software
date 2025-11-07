#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <array>
#include <cctype>
#include <iterator>

using namespace std;

// ------------------ 工具函数 ------------------

/**
 * @brief 计算CRC8校验码
 * @param data 数据指针
 * @param len 数据长度
 * @return 8位的CRC校验码
 */
uint8_t calcCRC8(const unsigned char* data, size_t len) {
    uint8_t crc = 0x00;
    const uint8_t poly = 0x07;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ poly);
            }
            else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief 将单个字节转换为两位十六进制字符串
 * @param b 字节
 * @return 十六进制表示的字符串
 */
string byteToHex(unsigned char b) {
    stringstream ss;
    ss << uppercase << hex << setw(2) << setfill('0') << (int)b;
    return ss.str();
}

/**
 * @brief 将6字节的MAC地址格式化为 "XX-XX-XX-XX-XX-XX"
 * @param data 包含MAC地址的字节向量
 * @param start MAC地址在向量中的起始索引
 * @return 格式化后的MAC地址字符串
 */
string macToStr(const vector<unsigned char>& data, int start) {
    stringstream ss;
    for (int i = 0; i < 6; i++) {
        ss << byteToHex(data[start + i]);
        if (i != 5) {
            ss << "-";
        }
    }
    return ss.str();
}

/**
 * @brief 将6字节的MAC地址格式化为 "XX-XX-XX-XX-XX-XX"
 * @param data 指向MAC地址数据的指针
 * @return 格式化后的MAC地址字符串
 */
string macToStr(const unsigned char* data) {
    stringstream ss;
    for (int i = 0; i < 6; i++) {
        ss << uppercase << hex << setw(2) << setfill('0') << (int)data[i];
        if (i != 5) {
            ss << "-";
        }
    }
    ss << dec; // 恢复为十进制流
    return ss.str();
}

/**
 * @brief 将字节数据格式化为ASCII字符串，不可打印字符显示为'.'
 * @param data 数据指针
 * @param len 数据长度
 * @return ASCII表示的字符串
 */
string formatDataAscii(const unsigned char* data, size_t len) {
    stringstream ss;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = data[i];
        ss << (isprint(c) ? (char)c : '.');
    }
    return ss.str();
}

// ------------------ 主函数 ------------------
int main(int argc, char* argv[]) {
    if (argc != 2) {
        cout << "用法: " << argv[0] << " [数据帧文件路径]" << endl;
        cout << "示例: Ethernet_Analyzer.exe input" << endl;
        return 0;
    }

    ifstream fin(argv[1], ios::binary);
    if (!fin) {
        cerr << "无法打开文件: " << argv[1] << endl;
        return 1;
    }

    vector<unsigned char> data((istreambuf_iterator<char>(fin)), istreambuf_iterator<char>());
    fin.close();

    size_t pos = 0;
    int frameCount = 0;
    const array<unsigned char, 8> preamble = { {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAB} };
    const size_t total = data.size();

    while (pos + 8 <= total) {
        // 1. 查找前导码 (7 x 0xAA + 0xAB)
        bool found = false;
        while (pos + 8 <= total) {
            if (memcmp(&data[pos], preamble.data(), preamble.size()) == 0) {
                found = true;
                break;
            }
            ++pos;
        }
        if (!found) {
            break; // 文件末尾未找到完整的前导码
        }

        size_t frameStart = pos; // 指向第一个0xAA
        size_t payloadStart = frameStart + 8; // 跳过7xAA + SFD，指向目的MAC地址

        // 至少需要 14字节头部 + 1字节FCS
        if (payloadStart + 14 + 1 > total) {
            break;
        }

        // 2. 通过查找下一个前导码来确定当前帧的边界
        size_t nextPreamble = total;
        for (size_t j = payloadStart + 1; j + 8 <= total; ++j) {
            if (memcmp(&data[j], preamble.data(), preamble.size()) == 0) {
                nextPreamble = j;
                break;
            }
        }

        // 3. 确定CRC(FCS)的位置
        size_t crcOffset = 0; // FCS的起始位置 (1 byte)
        if (nextPreamble == total) {
            // 这是文件中的最后一帧
            if (total < 1) break;
            crcOffset = total - 1;
        }
        else {
            // 检查下一帧是否离得太近
            if (nextPreamble < 1 || nextPreamble < payloadStart + 14 + 1) {
                pos = nextPreamble; // 当前帧不合法，跳到下一帧继续
                continue;
            }
            crcOffset = nextPreamble - 1;
        }

        size_t headerOffset = payloadStart;
        size_t dataStart = headerOffset + 14;

        if (crcOffset < dataStart) { // 确保数据段长度至少为0
            pos = (nextPreamble == total) ? total : nextPreamble;
            continue;
        }
        size_t dataLen = crcOffset - dataStart;

        // 新增：对数据字段长度进行检查
        const size_t ETH_MIN_PAYLOAD = 46; //以太网最小有效载荷
        const size_t ETH_MAX_PAYLOAD = 1500; //以太网最大有效载荷(不考虑Jumbo帧)
        bool length_ok = true;
        if (dataLen < ETH_MIN_PAYLOAD) {
            length_ok = false;
        }
        if (dataLen > ETH_MAX_PAYLOAD) {
            length_ok = false;
        }

        // 4. 读取文件中的FCS并计算CRC
        if (crcOffset + 1 > total) {
            break;
        }
        uint8_t fcs = data[crcOffset];

        // CRC8计算范围：从目的MAC地址到数据字段末尾（不包括FCS）
        size_t crcCalcLen = crcOffset - payloadStart;
        uint8_t calc = calcCRC8(&data[payloadStart], crcCalcLen);

        // 5. 输出解析结果
        ++frameCount;
        cout << "序号: " << setw(2) << setfill('0') << frameCount << endl;
        cout << "------------------------------------------" << endl;
        cout << "前导码:     ";
        for (int i = 0; i < 7; ++i) {
            cout << byteToHex(data[frameStart + i]) << " ";
        }
        cout << endl;
        cout << "帧前定界符: " << byteToHex(data[frameStart + 7]) << endl;

        cout << "目的地址:   " << macToStr(data, headerOffset) << endl;
        cout << "源地址:     " << macToStr(data, headerOffset + 6) << endl;

        // 类型字段（网络字节序）
        unsigned int et = ((unsigned int)data[headerOffset + 12] << 8) | data[headerOffset + 13];
        stringstream ss;
        ss << "0x" << hex << uppercase << setw(4) << setfill('0') << et << dec;
        cout << "类型字段:   " << ss.str() << endl;

        cout << "数据字段长度: " << dec << dataLen << " 字节" << endl;
        if (!length_ok) {
            if (dataLen < ETH_MIN_PAYLOAD) {
                cout << "长度异常: 数据字段长度过短（小于 " << ETH_MIN_PAYLOAD << " 字节），可能为截断帧或错误帧。" << endl;
            } else {
                cout << "长度异常: 数据字段长度过长（大于 " << ETH_MAX_PAYLOAD << " 字节），可能包含额外数据或错误。" << endl;
            }
        }

        cout << "数据字段(ASCII): " << formatDataAscii(&data[dataStart], dataLen) << endl;

        // 保存当前cout状态，以便在打印十六进制后恢复
        ios oldState(nullptr);
        oldState.copyfmt(cout);
        cout << "CRC校验(文件): 0x" << hex << uppercase << setw(2) << setfill('0') << (int)fcs << endl;
        cout << "CRC校验(计算): 0x" << hex << uppercase << setw(2) << setfill('0') << (int)calc << endl;
        cout.copyfmt(oldState); // 恢复cout状态

        // 状态：当且仅当CRC匹配且长度正常时接收
        cout << "状态:       " << ((fcs == calc && length_ok) ? "Accept" : "Reject") << endl;
        cout << "------------------------------------------" << endl << endl;

        // 从下一个前导码的位置继续搜索
        pos = (nextPreamble == total) ? total : nextPreamble;
    }

    if (frameCount == 0) {
        cout << "未检测到有效以太网帧。" << endl;
    }

    return 0;
}