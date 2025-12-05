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

using namespace boost::asio;
using boost::asio::ip::tcp;

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
