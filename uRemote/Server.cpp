#include "Server.h"

Server::Server(boost::asio::io_context& io_context, const std::string& port)
    : BaseConnection(io_context), m_acceptor(io_context), m_port(port) {
}

Server::~Server() {
    stop();
}

void Server::start() {
    setState(ConnectionState::CONNECTING, "CONNECTING");

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
    } catch (const std::exception& e) {
        setState(ConnectionState::ERR, "Server start error: " + std::string(e.what()));
    }
}

void Server::stop() {
    setState(ConnectionState::DISCONNECTING, "Server Stopping");

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