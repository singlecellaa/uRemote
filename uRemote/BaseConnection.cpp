#include "BaseConnection.h"

BaseConnection::BaseConnection(boost::asio::io_context& io_context)
    : m_io_context(io_context), m_socket(io_context), m_state(ConnectionState::DISCONNECTED) {
}

BaseConnection::~BaseConnection() {
    stop();
    close();
}

void BaseConnection::stop() {
    boost::system::error_code ec;
    m_socket.close(ec);
}

void BaseConnection::close() {
    if (m_io_thread.joinable()) {
        m_io_context.stop();
        m_io_thread.join();
    }
}

void BaseConnection::send(const NetworkMessage& message) {
    if (!isConnected()) return;

    auto processed_msg = preprocessSend(message);
    auto self = shared_from_this();

    boost::asio::async_write(m_socket,
        boost::asio::buffer(processed_msg.data),
        [this, self](const boost::system::error_code& error, size_t bytes_transferred) {
            handleWrite(error, bytes_transferred);
        });
}

void BaseConnection::send(const std::string& message) {
    NetworkMessage msg;
    msg.data.assign(message.begin(), message.end());
    send(msg);
}

void BaseConnection::setConnectionCallback(ConnectionCallback callback) {
    m_connection_callback = callback;
}

void BaseConnection::setMessageCallback(MessageCallback callback) {
    m_message_callback = callback;
}

void BaseConnection::setErrorCallback(ErrorCallback callback) {
    m_error_callback = callback;
}

ConnectionState BaseConnection::getState() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_state;
}

bool BaseConnection::isConnected() const {
    return getState() == ConnectionState::CONNECTED;
}

void BaseConnection::setState(ConnectionState new_state, const std::string& info) {
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_state = new_state;
    }

    if (m_connection_callback) {
        m_connection_callback(new_state, info);
    }
}

void BaseConnection::startReading() {
    auto self = shared_from_this();
    m_socket.async_read_some(boost::asio::buffer(m_read_buffer),
        [this, self](const boost::system::error_code& error, size_t bytes_transferred) {
            handleRead(error, bytes_transferred);
        });
}

void BaseConnection::handleRead(const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error) {
        NetworkMessage message;
        message.data.assign(m_read_buffer.begin(), m_read_buffer.begin() + bytes_transferred);

        auto processed_msg = preprocessReceive(message);

        // Call message callback
        if (m_message_callback) {
            m_message_callback(processed_msg);
        }

        // Call virtual method for derived classes
        onMessageReceived(processed_msg);

        // Continue reading
        startReading();
    }
    else {
        if (error != boost::asio::error::operation_aborted) {
            std::string error_msg = "Read error: " + error.message();
            if (m_error_callback) {
                m_error_callback(error_msg);
            }
            onError(error_msg);
            stop();
        }
    }
}

void BaseConnection::handleWrite(const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
        std::string error_msg = "Write error: " + error.message();
        if (m_error_callback) {
            m_error_callback(error_msg);
        }
        onError(error_msg);
        stop();
    }
}