#pragma once
#include "BaseConnection.h"

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
