#pragma once
#include  "network.h"

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
    void close();
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
