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

enum class Mode {
    SERVER,
    CLIENT,
    NONE
};

struct ConnectionEvent {
    ConnectionState type;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

enum class MessageType {
    TEXT,
    COMMAND,
    TERMIAL_OUTPUT,
    BINARY
};

// Message structure
struct NetworkMessage {
	MessageType type;
    std::vector<uint8_t> data;
    std::string toString() const {
        return std::string(data.begin(), data.end());
    }
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer;

        // Type (1 byte)
        buffer.push_back(static_cast<uint8_t>(type));

        // Size (4 bytes in network byte order)
        uint32_t net_size = htonl(static_cast<uint32_t>(data.size()));
        const uint8_t* size_bytes = reinterpret_cast<const uint8_t*>(&net_size);
        buffer.insert(buffer.end(), size_bytes, size_bytes + 4);

        // Data
        buffer.insert(buffer.end(), data.begin(), data.end());

        return buffer;
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
    std::deque<std::string> m_received_messages;
    std::mutex m_received_messages_mutex;

    // Thread-safe signal queue
    std::deque<ConnectionState> m_signal_queue;
    std::mutex m_signal_mutex;

    // Thread-safe event queue
    std::deque<NetworkMessage> m_message_queue;
    std::mutex m_message_mutex;

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

    void pushSignal(ConnectionState state);
    std::vector<ConnectionState> popSignals();

    void pushNetworkMessage(const NetworkMessage& msg);
    std::vector<NetworkMessage> popNetworkMessages();

    void sendMessage(const std::string& message);
    void sendMessage(const NetworkMessage& message);
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


static inline std::string getLocalConnectedIP() {
    std::string localIP;

#ifdef _WIN32
    // Windows: 使用 GetAdapterAddresses API
    ULONG bufferSize = 150000;
    PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
    DWORD result = ERROR_BUFFER_OVERFLOW;

    // 申请足够内存来存储适配器信息
    do {
        pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(bufferSize);
        if (pAddresses == nullptr) return "";

        // 调用 GetAdapterAddresses，请求包含网关信息
        result = GetAdaptersAddresses(
            AF_UNSPEC,
            GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_ALL_INTERFACES,
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

    // 遍历适配器链表，寻找第一个具有IPv4默认网关的适配器，并获取其IPv4地址
    PIP_ADAPTER_ADDRESSES pCurrAddress = pAddresses;
    while (pCurrAddress) {
        // 检查适配器是否已连接且运行中
        if (pCurrAddress->OperStatus == IfOperStatusUp) {
            // 检查是否有IPv4默认网关
            bool hasIPv4Gateway = false;
            PIP_ADAPTER_GATEWAY_ADDRESS_LH pGateway = pCurrAddress->FirstGatewayAddress;
            while (pGateway) {
                if (pGateway->Address.lpSockaddr->sa_family == AF_INET) {
                    hasIPv4Gateway = true;
                    break;
                }
                pGateway = pGateway->Next;
            }

            // 如果有IPv4默认网关，则获取该适配器的IPv4地址
            if (hasIPv4Gateway) {
                PIP_ADAPTER_UNICAST_ADDRESS_LH pUnicast = pCurrAddress->FirstUnicastAddress;
                while (pUnicast) {
                    if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                        sockaddr_in* sa_in = (sockaddr_in*)(pUnicast->Address.lpSockaddr);
                        localIP = inet_ntoa(sa_in->sin_addr);
                        break;
                    }
                    pUnicast = pUnicast->Next;
                }

                if (!localIP.empty()) {
                    break; // 找到连接网络的IP地址，退出循环
                }
            }
        }
        pCurrAddress = pCurrAddress->Next;
    }

    if (pAddresses) free(pAddresses);

#else
    // Linux: 通过解析 /proc/net/route 文件获取默认网关接口，然后获取该接口的IP地址
    std::string interfaceName;

    // 第一步：从/proc/net/route获取具有默认网关的接口名
    std::ifstream routeFile("/proc/net/route");
    std::string line;

    // 跳过第一行标题
    std::getline(routeFile, line);

    while (std::getline(routeFile, line)) {
        char iface[256];
        unsigned long destination, gateway, flags;

        if (sscanf(line.c_str(), "%255s %lx %lx %*s %*s %*s %*s %lx",
            iface, &destination, &gateway, &flags) == 4) {
            // 检查是否是默认路由（目标地址为0）并且网关不为0
            if (destination == 0 && gateway != 0) {
                interfaceName = iface;
                break;
            }
        }
    }
    routeFile.close();

    // 第二步：通过getifaddrs获取指定接口的IPv4地址
    if (!interfaceName.empty()) {
        struct ifaddrs* ifaddr, * ifa;

        if (getifaddrs(&ifaddr) == -1) {
            return "";
        }

        // 遍历所有接口
        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) {
                continue;
            }

            // 检查接口名是否匹配并且是IPv4地址
            if (interfaceName == ifa->ifa_name && ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(sa->sin_addr), ipStr, INET_ADDRSTRLEN);
                localIP = ipStr;
                break;
            }
        }

        freeifaddrs(ifaddr);
    }

    // 备选方案：如果上述方法失败，尝试使用ioctl获取IP
    if (localIP.empty() && !interfaceName.empty()) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, interfaceName.c_str(), IFNAMSIZ - 1);

            if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
                struct sockaddr_in* sa = (struct sockaddr_in*)&ifr.ifr_addr;
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(sa->sin_addr), ipStr, INET_ADDRSTRLEN);
                localIP = ipStr;
            }
            close(fd);
        }
    }
#endif

    return localIP;
}
