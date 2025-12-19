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

void BaseConnection::send(const std::string& message) {
    NetworkMessage msg;
    msg.type = MessageType::TEXT;
    msg.data.assign(message.begin(), message.end());
    send(msg);
}

void BaseConnection::send(const NetworkMessage& message) {
    if (!isConnected()) return;

    NetworkMessage processed_msg = preprocessSend(message);
    std::shared_ptr<BaseConnection> self = shared_from_this();
    
    // Create a shared_ptr to the message data to keep it alive during async operation
    auto serialized = std::make_shared<std::vector<uint8_t>>(processed_msg.serialize());

    boost::asio::async_write(m_socket,
        boost::asio::buffer(*serialized),
        [this, self, serialized](const boost::system::error_code& error, size_t bytes_transferred) {
            handleWrite(error, bytes_transferred);
        });
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
        message.type = MessageType(m_read_buffer[0]);
        uint32_t net_size;
		std::memcpy(&net_size, m_read_buffer.data() + 1, 4);
		uint32_t data_size = ntohl(net_size);
        message.data.assign(m_read_buffer.begin() + 5, m_read_buffer.begin() + 5 + data_size);

        auto processed_msg = preprocessReceive(message);

        if (m_message_callback) {
            m_message_callback(processed_msg);
        }

        onMessageReceived(processed_msg);

        startReading();
    } else {
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
