// ÐÞÕý°æ£¨È¥µô icmp.lib ÒÀÀµ£©
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace std;

static string formatMac(const BYTE* addr, ULONG len) {
 if (len ==0) return "";
 ostringstream oss;
 oss << hex << setfill('0');
 for (ULONG i =0; i < len; ++i) {
 if (i) oss << ':';
 oss << setw(2) << static_cast<int>(addr[i]);
 }
 return oss.str();
}

static bool isZeroMac(const BYTE* addr, ULONG len) {
 for (ULONG i =0; i < len; ++i) if (addr[i] !=0) return false;
 return true;
}

int main() {
 WSADATA wsaData;
 if (WSAStartup(MAKEWORD(2,2), &wsaData) !=0) {
 cerr << "WSAStartup failed" << endl;
 return 1;
 }

 ULONG outBufLen =0;
 DWORD dwRet = GetAdaptersInfo(nullptr, &outBufLen);
 if (dwRet != ERROR_BUFFER_OVERFLOW) {
 cerr << "GetAdaptersInfo failed to get buffer size: " << dwRet << endl;
 WSACleanup();
 return 1;
 }

 vector<BYTE> buffer(outBufLen);
 PIP_ADAPTER_INFO pAdapterInfo = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
 dwRet = GetAdaptersInfo(pAdapterInfo, &outBufLen);
 if (dwRet != NO_ERROR) {
 cerr << "GetAdaptersInfo failed: " << dwRet << endl;
 WSACleanup();
 return 1;
 }

 cout << "Network adapters and scan results:" << endl;

 for (PIP_ADAPTER_INFO pAdapter = pAdapterInfo; pAdapter != nullptr; pAdapter = pAdapter->Next) {
 cout << "------------------------------------------------------------" << endl;
 cout << "Adapter: " << pAdapter->AdapterName << " (" << pAdapter->Description << ")" << endl;
 cout << "MAC: " << formatMac(pAdapter->Address, pAdapter->AddressLength) << endl;

 for (IP_ADDR_STRING* ipAddr = &pAdapter->IpAddressList; ipAddr != nullptr; ipAddr = ipAddr->Next) {
 if (ipAddr->IpAddress.String[0] == '\0') break;
 string ipStr = ipAddr->IpAddress.String;
 string maskStr = ipAddr->IpMask.String;
 if (ipStr == "0.0.0.0") continue;

 cout << "IP: " << ipStr << " Mask: " << maskStr << " Gateway: " << pAdapter->GatewayList.IpAddress.String << endl;

 uint32_t ip = ntohl(inet_addr(ipStr.c_str()));
 uint32_t mask = ntohl(inet_addr(maskStr.c_str()));
 if (mask ==0) continue;

 uint32_t network = ip & mask;
 uint32_t broadcast = network | (~mask);

 cout << "Scanning subnet: " << ((network >>24) &0xFF) << "." << ((network >>16) &0xFF) << "." << ((network >>8) &0xFF) << "." << (network &0xFF)
 << " - " << ((broadcast >>24) &0xFF) << "." << ((broadcast >>16) &0xFF) << "." << ((broadcast >>8) &0xFF) << "." << (broadcast &0xFF) << endl;

 uint32_t start = network +1;
 uint32_t end = (broadcast >0) ? (broadcast -1) : broadcast;
 uint64_t hostCount = (end >= start) ? (uint64_t)(end - start +1) :0;
 if (hostCount ==0) {
 cout << "No hosts to scan on this interface." << endl;
 continue;
 }
 if (hostCount >65534) {
 cout << "Subnet too large (" << hostCount << " hosts). Skipping detailed scan." << endl;
 continue;
 }

 vector<uint32_t> candidates;
 candidates.reserve(static_cast<size_t>(hostCount));
 for (uint32_t h = start; h <= end; ++h) candidates.push_back(htonl(h));

 // Thread count: use hardware_concurrency if available, otherwise4. Cap to reasonable max and to candidate count.
 unsigned int hc = thread::hardware_concurrency();
 if (hc ==0) hc =4;
 unsigned int maxThreads = hc;
 if (maxThreads >64) maxThreads =64;
 if (maxThreads > candidates.size()) maxThreads = static_cast<unsigned int>(candidates.size());
 if (maxThreads ==0) maxThreads =1;

 cout << "Using " << maxThreads << " threads to scan " << candidates.size() << " hosts..." << endl;

 atomic<size_t> nextIndex(0);
 mutex printMutex;

 auto worker = [&](unsigned int /*threadId*/) {
 for (;;) {
 size_t idx = nextIndex.fetch_add(1);
 if (idx >= candidates.size()) break;
 uint32_t candidate = candidates[idx];
 BYTE macAddr[8] = {0};
 ULONG macAddrLen = sizeof(macAddr);
 DWORD arpRet = SendARP((IPAddr)candidate,0, macAddr, &macAddrLen);
 if (arpRet == NO_ERROR && macAddrLen >0 && !isZeroMac(macAddr, macAddrLen)) {
 in_addr a;
 a.S_un.S_addr = candidate;
 const char* ipBuf = inet_ntoa(a);
 string macs = formatMac(macAddr, macAddrLen);
 lock_guard<mutex> lk(printMutex);
 cout << ipBuf << " -> " << macs << endl;
 }
 }
 };

 vector<thread> threads;
 threads.reserve(maxThreads);
 for (unsigned int t =0; t < maxThreads; ++t) threads.emplace_back(worker, t);
 for (auto &th : threads) th.join();

 } // ipAddr
 } // adapter

 WSACleanup();
 return 0;
}
