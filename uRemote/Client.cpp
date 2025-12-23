#include "Client.h"

Client::Client(boost::asio::io_context& io_context, const std::string& host, const std::string& port, const std::string& password)
    : BaseConnection(io_context), m_host(host), m_port(port), m_password(password), m_resolver(io_context) {
}

Client::~Client() {
    stop();
}

void Client::start() {
    setState(ConnectionState::CONNECTING, "CONNECTING");
    startConnect();

    // Start IO context in separate thread
    m_io_thread = std::thread([this]() {
        m_io_context.run();
        });
}

void Client::stop() {
    setState(ConnectionState::DISCONNECTING, "Client Stopping");

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
        setState(ConnectionState::AUTHENTICATING, "Authenticating...");
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
    // Send authentication request
    NetworkMessage auth_msg;
    auth_msg.fromAuthRequest(m_password);
    send(auth_msg);
	std::cout << "Client: Sent authentication request" << std::endl;
}

void Client::onDisconnected() {
    std::cout << "Client: Disconnected from server" << std::endl;
}

void Client::onError(const std::string& error_message) {
    std::cerr << "Client error: " << error_message << std::endl;
}