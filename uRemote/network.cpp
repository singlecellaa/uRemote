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
        pushConnectionEvent(state, info);
        handleConnectionState("Server", state, info); 
     });

    m_server->setMessageCallback([this](const NetworkMessage& message)
        { handleMessage("Server", message); });

    m_server->setErrorCallback([this](const std::string& error)
        { handleError("Server", error); });

    m_server->start();
    updateConnectionInfo("Server started on port " + port);
}

void NetworkManager::startClient(const std::string& host, const std::string& port) {
    stopAll();

    m_client_io_context = std::make_unique<boost::asio::io_context>();
    m_client = std::make_shared<Client>(*m_client_io_context, host, port);

    m_client->setConnectionCallback([this](ConnectionState state, const std::string& info) { 
        pushConnectionEvent(state,info);
        handleConnectionState("Client", state, info); 
    });

    m_client->setMessageCallback([this](const NetworkMessage& message)
        { handleMessage("Client", message); });

    m_client->setErrorCallback([this](const std::string& error)
        { handleError("Client", error); });

    m_client->start();
    updateConnectionInfo("Connecting to " + host + ":" + port + "...");
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

void NetworkManager::pushConnectionEvent(ConnectionState state, const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_event_mutex);
    m_event_queue.push_back({ state, msg, std::chrono::system_clock::now() });
}

std::vector<ConnectionEvent> NetworkManager::popEvents() {
    std::lock_guard<std::mutex> lock(m_event_mutex);
    std::vector<ConnectionEvent> events(m_event_queue.begin(), m_event_queue.end());
    m_event_queue.clear();
    return events;
}

void NetworkManager::sendMessage(const std::string& message) {
    if (m_server && m_server->isConnected()) {
        m_server->send(message);
        addLocalMessage("Sent to client: " + message);
    } else if (m_client && m_client->isConnected()) {
        m_client->send(message);
        addLocalMessage("Sent to server: " + message);
    } else {
        addLocalMessage("Not connected - message not sent: " + message);
    }
}

std::vector<std::string> NetworkManager::getMessages() {
    std::lock_guard<std::mutex> lock(m_messages_mutex);
    return m_received_messages;
}

void NetworkManager::clearMessages() {
    std::lock_guard<std::mutex> lock(m_messages_mutex);
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

    printf("%s\n", message.c_str());
}

void NetworkManager::handleMessage(const std::string &type, const NetworkMessage &message) {
    std::string msg_str = message.toString();
    std::string display_msg = type + " received: " + msg_str;
    addLocalMessage(display_msg);
    printf("%s\n", display_msg.c_str());
}

void NetworkManager::handleError(const std::string &type, const std::string &error) {
    std::string error_msg = type + " error: " + error;
    addLocalMessage(error_msg);
    printf("%s\n", error_msg.c_str());
}

void NetworkManager::setConnectionState(ConnectionState state) {
    m_connection_state = state;
}

void NetworkManager::updateConnectionInfo(const std::string &info) {
    std::lock_guard<std::mutex> lock(m_info_mutex);
    m_connection_info = info;
}

void NetworkManager::addLocalMessage(const std::string &message) {
    std::lock_guard<std::mutex> lock(m_messages_mutex);
    m_received_messages.push_back(message);

    // Keep only last 100 messages to prevent memory growth
    if (m_received_messages.size() > 100) {
        m_received_messages.erase(m_received_messages.begin());
    }
}