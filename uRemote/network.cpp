#include "network.h"
#include "Server.h"
#include "Client.h"

NetworkManager::~NetworkManager() {
    stopAll();
}

void NetworkManager::startServer(const std::string& port) {
    stopAll();

    m_server_io_context = std::make_unique<boost::asio::io_context>();
    m_server = std::make_shared<Server>(*m_server_io_context, port);

    m_server->setConnectionCallback([this](ConnectionState state, const std::string& info) { 
        if (state == ConnectionState::CONNECTED || state == ConnectionState::DISCONNECTED) {
            SignalType signal = (state == ConnectionState::CONNECTED) ? SignalType::CONNECTED : SignalType::DISCONNECTED;
            pushSignal(signal);
        }
        handleConnectionState("Server", state, info);
    });

    m_server->setMessageCallback([this](const NetworkMessage& message) { 
        handleMessage("Server", message);
    });

    m_server->setErrorCallback([this](const std::string& error) { 
        handleError("Server", error);
    });

    m_server->start();
    updateConnectionInfo("Server started on port " + port);
}

void NetworkManager::startClient(const std::string& host, const std::string& port) {
    stopAll();

    m_client_io_context = std::make_unique<boost::asio::io_context>();
    m_client = std::make_shared<Client>(*m_client_io_context, host, port);

    m_client->setConnectionCallback([this](ConnectionState state, const std::string& info) { 
        if (state == ConnectionState::CONNECTED || state == ConnectionState::DISCONNECTED) {
			SignalType signal = (state == ConnectionState::CONNECTED) ? SignalType::CONNECTED : SignalType::DISCONNECTED;
            pushSignal(signal);
        }
        handleConnectionState("Client", state, info);
    });

    m_client->setMessageCallback([this](const NetworkMessage& message)
        { handleMessage("Client", message); });

    m_client->setErrorCallback([this](const std::string& error)
        { handleError("Client", error); });

    m_client->start();
    updateConnectionInfo("Connecting to " + host + ":" + port + "...");
}

void NetworkManager::handleMessage(const std::string& type, const NetworkMessage& message) {
    if (message.type == MessageType::TEXT) {
        std::string msg_str = message.toString();
        std::string display_msg = type + " received: " + msg_str;
        addLocalMessage(display_msg);
        std::cout << "received text message: " << msg_str << std::endl;
    } else if (message.type == MessageType::COMMAND) {
        pushNetworkMessage(message);
		std::cout << "pushed command message: " << message.toString() << std::endl;
    } else if (message.type == MessageType::TERMIAL_OUTPUT) {
        pushNetworkMessage(message);
		std::cout << "pushed terminal output message: " << message.toString() << std::endl;
    } else if (message.type == MessageType::SIGNAL) {
		pushNetworkMessage(message);
    } else if (message.type == MessageType::FILESYSTEM_REQUEST) {
        pushNetworkMessage(message);
		std::cout << "pushed filesystem request message: " << message.toFilesystemRequest() << std::endl;
    } else if (message.type == MessageType::FILESYSTEM_RESPONSE) {
        pushNetworkMessage(message);
		std::cout << "pushed filesystem response message" << std::endl;
    } else if (message.type == MessageType::ERR) {
        pushNetworkMessage(message);
		std::cout << "pushed error message: " << message.toError() << std::endl;
    }
}

void NetworkManager::stopAll() {
    if (m_server) {
        m_server->stop();
        m_server->close();
        m_server.reset();
        m_server_io_context.reset();
        
    }
    if (m_client) {
        m_client->stop();
        m_client->close();
        m_client.reset();
        m_client_io_context.reset();
    }
    updateConnectionInfo("Stopped");
}

void NetworkManager::pushSignal(SignalType signal) {
    std::lock_guard<std::mutex> lock(m_signal_mutex);
    m_signal_queue.push_back(signal);
}

std::vector<SignalType> NetworkManager::popSignals() {
    std::lock_guard<std::mutex> lock(m_signal_mutex);
    std::vector<SignalType> signals(m_signal_queue.begin(), m_signal_queue.end());
    m_signal_queue.clear();
    return signals;
}

void NetworkManager::pushNetworkMessage(const NetworkMessage& msg) {
    std::lock_guard<std::mutex> lock(m_message_mutex);
    m_message_queue.push_back(msg);
}

std::vector<NetworkMessage> NetworkManager::popNetworkMessages() {
    std::lock_guard<std::mutex> lock(m_message_mutex);
    std::vector<NetworkMessage> messages(m_message_queue.begin(), m_message_queue.end());
    m_message_queue.clear();
    return messages;
}

void NetworkManager::sendMessage(const std::string& message) {
    if (m_server && m_server->isConnected()) {
        m_server->send(message);
        addLocalMessage("Sent to client: " + message);
        std::cout << "sent message to client: " << message << std::endl;
    } else if (m_client && m_client->isConnected()) {
        m_client->send(message);
        addLocalMessage("Sent to server: " + message);
        std::cout << "sent message to server: " << message << std::endl;
    } else {
        addLocalMessage("Not connected - message not sent: " + message);
        std::cout << "not connected - message not sent: " << message << std::endl;
    }
}

void NetworkManager::sendMessage(const NetworkMessage& message) {
    if (m_server && m_server->isConnected()) {
        m_server->send(message);
        addLocalMessage("Sent to client: " + message.toString());
    } else if (m_client && m_client->isConnected()) {
        m_client->send(message);
        addLocalMessage("Sent to server: " + message.toString());
    } else {
        addLocalMessage("Not connected - message not sent: " + message.toString());
    }
}

std::vector<std::string> NetworkManager::getMessages() {
    std::lock_guard<std::mutex> lock(m_received_messages_mutex);
    std::vector<std::string> messages(m_received_messages.begin(), m_received_messages.end());
    return messages;
}

void NetworkManager::clearMessages() {
    std::lock_guard<std::mutex> lock(m_received_messages_mutex);
    m_received_messages.clear();
}

ConnectionState NetworkManager::getConnectionState() const {
    return m_connection_state;
}

std::string NetworkManager::getConnectionInfo() const {
    std::lock_guard<std::mutex> lock(m_info_mutex);
    return m_connection_info;
}

bool NetworkManager::isConnected() const {
    return m_connection_state == ConnectionState::CONNECTED;
}

bool NetworkManager::isServerMode() const {
    return m_server && m_server->isConnected();
}

bool NetworkManager::isClientMode() const {
    return m_client && m_client->isConnected();
}

void NetworkManager::handleConnectionState(const std::string &type, ConnectionState state, const std::string &info) {
    setConnectionState(state);
    updateConnectionInfo(type + ": " + info);

    std::string message = type + " state: " + info;
    addLocalMessage(message);

    std::cout << "handle connection state - " << message << std::endl;
}

void NetworkManager::handleError(const std::string &type, const std::string &error) {
    std::string error_msg = type + " error: " + error;
    addLocalMessage(error_msg);
    std::cerr << "handle error - " << error_msg << std::endl;
}

void NetworkManager::setConnectionState(ConnectionState state) {
    m_connection_state = state;
}

void NetworkManager::updateConnectionInfo(const std::string &info) {
    std::lock_guard<std::mutex> lock(m_info_mutex);
    m_connection_info = info;
}

void NetworkManager::addLocalMessage(const std::string &message) {
    std::lock_guard<std::mutex> lock(m_received_messages_mutex);
    m_received_messages.push_back(message);

    // Keep only last 100 messages to prevent memory growth
    if (m_received_messages.size() > 100) {
        m_received_messages.pop_front();
    }
}