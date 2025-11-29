#include "network.h"
#include <iostream>

// BaseConnection implementation
BaseConnection::BaseConnection(boost::asio::io_context& io_context)
    : m_io_context(io_context), m_socket(io_context), m_state(ConnectionState::DISCONNECTED) {
}

BaseConnection::~BaseConnection() {
    stop();
}

void BaseConnection::stop() {
    setState(ConnectionState::DISCONNECTING);

    boost::system::error_code ec;
    m_socket.close(ec);

    if (m_io_thread.joinable()) {
        m_io_context.stop();
        m_io_thread.join();
    }

    setState(ConnectionState::DISCONNECTED);
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

// Server implementation
Server::Server(boost::asio::io_context& io_context, const std::string& port)
    : BaseConnection(io_context), m_acceptor(io_context), m_port(port) {
}

Server::~Server() {
    stop();
}

void Server::start() {
    setState(ConnectionState::CONNECTING);

    try {
        tcp::endpoint endpoint(tcp::v4(), std::stoi(m_port));
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(tcp::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen();

        startAccept();

        // Start IO context in separate thread
        m_io_thread = std::thread([this]() {
            m_io_context.run();
            });

    }
    catch (const std::exception& e) {
        setState(ConnectionState::ERR, e.what());
    }
}

void Server::stop() {
    setState(ConnectionState::DISCONNECTING);
    
    // Close acceptor first to stop accepting new connections
    boost::system::error_code ec;
    m_acceptor.close(ec);
    if (ec) {
        std::cerr << "Error closing acceptor: " << ec.message() << std::endl;
    }
    
    // Then call base class to close socket and stop IO context
    BaseConnection::stop();
    
    setState(ConnectionState::DISCONNECTED, "Server stopped");
}

void Server::startAccept() {
    m_acceptor.async_accept(m_socket,
        [this](const boost::system::error_code& error) {
            handleAccept(error);
        });
}

void Server::handleAccept(const boost::system::error_code& error) {
    if (!error) {
        setState(ConnectionState::CONNECTED, "Client connected");
        onConnected();
        startReading();
    }
    else {
        setState(ConnectionState::ERR, "Accept error: " + error.message());
        onError("Accept error: " + error.message());
    }
}

void Server::onConnected() {
    std::cout << "Server: Client connected" << std::endl;
}

void Server::onDisconnected() {
    std::cout << "Server: Client disconnected" << std::endl;
    // Restart accepting new connections
    if (getState() != ConnectionState::DISCONNECTING) {
        m_socket = tcp::socket(m_io_context);
        startAccept();
    }
}

void Server::onMessageReceived(const NetworkMessage& message) {
    std::cout << "Server received: " << message.toString() << std::endl;
}

void Server::onError(const std::string& error_message) {
    std::cerr << "Server error: " << error_message << std::endl;
}

// Client implementation
Client::Client(boost::asio::io_context& io_context, const std::string& host, const std::string& port)
    : BaseConnection(io_context), m_host(host), m_port(port), m_resolver(io_context) {
}

Client::~Client() {
    stop();
}

void Client::start() {
    setState(ConnectionState::CONNECTING);
    startConnect();

    // Start IO context in separate thread
    m_io_thread = std::thread([this]() {
        m_io_context.run();
        });
}

void Client::stop() {
    setState(ConnectionState::DISCONNECTING);
    
    // Cancel any pending resolver operations
    boost::system::error_code ec;
    m_resolver.cancel();
    
    // Then call base class to close socket and stop IO context
    BaseConnection::stop();
    
    setState(ConnectionState::DISCONNECTED, "Client stopped");
}

void Client::startConnect() {
    m_resolver.async_resolve(m_host, m_port,
        [this](const boost::system::error_code& error, tcp::resolver::results_type endpoints) {
            handleResolve(error, endpoints);
        });
}

void Client::handleResolve(const boost::system::error_code& error, tcp::resolver::results_type endpoints) {
    if (!error) {
        auto self = shared_from_this();
        boost::asio::async_connect(m_socket, endpoints,
            [this, self](const boost::system::error_code& error, const tcp::endpoint&) {
                handleConnect(error);
            });
    }
    else {
        setState(ConnectionState::ERR, "Resolve error: " + error.message());
        onError("Resolve error: " + error.message());
    }
}

void Client::handleConnect(const boost::system::error_code& error) {
    if (!error) {
        setState(ConnectionState::CONNECTED, "Connected to server");
        onConnected();
        startReading();
    }
    else {
        setState(ConnectionState::ERR, "Connect error: " + error.message());
        onError("Connect error: " + error.message());
    }
}

void Client::onConnected() {
    std::cout << "Client: Connected to server" << std::endl;
}

void Client::onDisconnected() {
    std::cout << "Client: Disconnected from server" << std::endl;
}

void Client::onMessageReceived(const NetworkMessage& message) {
    std::cout << "Client received: " << message.toString() << std::endl;
}

void Client::onError(const std::string& error_message) {
    std::cerr << "Client error: " << error_message << std::endl;
}