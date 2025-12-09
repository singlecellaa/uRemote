#pragma once
#include <boost/asio.hpp>
#include <thread>
#include <memory>
#include <functional>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>

// To get the ip of default gateway
#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <fstream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace boost::asio;
using namespace boost::asio::ip;

class BaseConnection;
class Server;
class Client;

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    ERR
};

// Message structure
struct NetworkMessage {
    std::vector<uint8_t> data;
    std::string toString() const {
        return std::string(data.begin(), data.end());
    }
};

// Callback types
using ConnectionCallback = std::function<void(ConnectionState, const std::string&)>;
using MessageCallback = std::function<void(const NetworkMessage&)>;
using ErrorCallback = std::function<void(const std::string&)>;


class NetworkManager {
private:
    std::shared_ptr<Server> m_server;
    std::shared_ptr<Client> m_client;
    std::unique_ptr<boost::asio::io_context> m_server_io_context;
    std::unique_ptr<boost::asio::io_context> m_client_io_context;

    // Thread-safe message queue
    std::vector<std::string> m_received_messages;
    std::mutex m_messages_mutex;

    // Connection status
    std::atomic<ConnectionState> m_connection_state{ ConnectionState::DISCONNECTED };
    std::string m_connection_info;
    mutable std::mutex m_info_mutex;

public:
    NetworkManager() = default;

    ~NetworkManager();

    void startServer(const std::string& port);

    void startClient(const std::string& host, const std::string& port);

    void stopAll();
    void sendMessage(const std::string& message);
    std::vector<std::string> getMessages();
    void clearMessages();
    ConnectionState getConnectionState() const;
    std::string getConnectionInfo() const;
    bool isConnected() const;
    bool isServerMode() const;
    bool isClientMode() const;

private:
    void handleConnectionState(const std::string& type, ConnectionState state, const std::string& info);
    void handleMessage(const std::string& type, const NetworkMessage& message);
    void handleError(const std::string& type, const std::string& error);
    void setConnectionState(ConnectionState state);
    void updateConnectionInfo(const std::string& info);
    void addLocalMessage(const std::string& message);
};


static inline std::string getDefaultGatewayIP() {
    std::string gatewayIP;

#ifdef _WIN32
    // Windows: 使用 GetAdapterAddresses API [citation:1]
    ULONG bufferSize = 15000; // 初始缓冲区大小
    PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
    DWORD result = ERROR_BUFFER_OVERFLOW;

    // 申请足够内存来存储适配器信息
    do {
        pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(bufferSize);
        if (pAddresses == nullptr) return "";

        // 调用 GetAdapterAddresses，请求包含网关信息 [citation:1]
        result = GetAdaptersAddresses(
            AF_UNSPEC, // 获取IPv4和IPv6地址
            GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_ALL_INTERFACES, // 关键标志 [citation:1]
            nullptr,
            pAddresses,
            &bufferSize
        );

        if (result == ERROR_BUFFER_OVERFLOW) {
            free(pAddresses);
            pAddresses = nullptr;
        }
        else if (result != NO_ERROR) {
            free(pAddresses);
            return "";
        }
    } while (result == ERROR_BUFFER_OVERFLOW);

    // 遍历适配器链表，寻找第一个有效的IPv4默认网关
    PIP_ADAPTER_ADDRESSES pCurrAddress = pAddresses;
    while (pCurrAddress && gatewayIP.empty()) {
        PIP_ADAPTER_GATEWAY_ADDRESS_LH pGateway = pCurrAddress->FirstGatewayAddress;
        while (pGateway) {
            if (pGateway->Address.lpSockaddr->sa_family == AF_INET) { // IPv4地址
                sockaddr_in* sa_in = (sockaddr_in*)(pGateway->Address.lpSockaddr);
                gatewayIP = inet_ntoa(sa_in->sin_addr);
                break;
            }
            pGateway = pGateway->Next;
        }
        pCurrAddress = pCurrAddress->Next;
    }

    if (pAddresses) free(pAddresses);

#else
    // Linux: 通过解析 /proc/net/route 文件获取默认网关
    std::ifstream routeFile("/proc/net/route");
    std::string line;

    // 跳过第一行标题
    std::getline(routeFile, line);

    while (std::getline(routeFile, line)) {
        char iface[256];
        unsigned long destination, gateway, flags;

        // 解析路由表的每一行
        if (sscanf(line.c_str(), "%255s %lx %lx %*s %*s %*s %*s %lx",
            iface, &destination, &gateway, &flags) == 4) {
            // 检查是否是默认路由（目标地址为0）并且网关不为0
            if (destination == 0 && gateway != 0) {
                // 将十六进制格式的网关地址转换为点分十进制
                unsigned char bytes[4];
                bytes[0] = gateway & 0xFF;
                bytes[1] = (gateway >> 8) & 0xFF;
                bytes[2] = (gateway >> 16) & 0xFF;
                bytes[3] = (gateway >> 24) & 0xFF;

                char ipStr[INET_ADDRSTRLEN];
                snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
                    bytes[0], bytes[1], bytes[2], bytes[3]);
                gatewayIP = ipStr;
                break;
            }
        }
    }
#endif

    return gatewayIP;
}
