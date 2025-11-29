#pragma once

#include <boost/asio.hpp>
#include <thread>
#include <memory>
#include <functional>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

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

// Base connection class for common functionality
class BaseConnection : public std::enable_shared_from_this<BaseConnection> {
protected:
    boost::asio::io_context& m_io_context;
    tcp::socket m_socket;
    ConnectionState m_state;
    std::thread m_io_thread;
    mutable std::mutex m_state_mutex;

    // Message queue for thread-safe communication
    std::queue<NetworkMessage> m_message_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;

    // Callbacks
    ConnectionCallback m_connection_callback;
    MessageCallback m_message_callback;
    ErrorCallback m_error_callback;

    // Buffer for reading
    std::array<uint8_t, 8192> m_read_buffer;

public:
    BaseConnection(boost::asio::io_context& io_context);
    virtual ~BaseConnection();

    // Common interface
    virtual void start() = 0;
    virtual void stop();
    virtual void send(const NetworkMessage& message);
    virtual void send(const std::string& message);

    // Callback setters
    void setConnectionCallback(ConnectionCallback callback);
    void setMessageCallback(MessageCallback callback);
    void setErrorCallback(ErrorCallback callback);

    // State management
    ConnectionState getState() const;
    bool isConnected() const;

protected:
    void setState(ConnectionState new_state, const std::string& info = "");
    void startReading();
    void handleRead(const boost::system::error_code& error, size_t bytes_transferred);
    void handleWrite(const boost::system::error_code& error, size_t bytes_transferred);

    // Virtual methods for extension points
    virtual void onConnected() {}
    virtual void onDisconnected() {}
    virtual void onMessageReceived(const NetworkMessage& message) {}
    virtual void onError(const std::string& error_message) {}

    // Message processing (can be overridden for encryption, etc.)
    virtual NetworkMessage preprocessSend(NetworkMessage message) { return message; }
    virtual NetworkMessage preprocessReceive(NetworkMessage message) { return message; }
};

// Server class
class Server : public BaseConnection {
private:
    tcp::acceptor m_acceptor;
    std::string m_port;

public:
    Server(boost::asio::io_context& io_context, const std::string& port);
    ~Server();

    void start() override;
    void stop() override;

private:
    void startAccept();
    void handleAccept(const boost::system::error_code& error);

    // Override base callbacks
    void onConnected() override;
    void onDisconnected() override;
    void onMessageReceived(const NetworkMessage& message) override;
    void onError(const std::string& error_message) override;
};

// Client class
class Client : public BaseConnection {
private:
    std::string m_host;
    std::string m_port;
    tcp::resolver m_resolver;

public:
    Client(boost::asio::io_context& io_context, const std::string& host, const std::string& port);
    ~Client();

    void start() override;
    void stop() override;

private:
    void startConnect();
    void handleConnect(const boost::system::error_code& error);
    void handleResolve(const boost::system::error_code& error, tcp::resolver::results_type endpoints);

    // Override base callbacks
    void onConnected() override;
    void onDisconnected() override;
    void onMessageReceived(const NetworkMessage& message) override;
    void onError(const std::string& error_message) override;
};
