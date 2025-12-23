#pragma once
#include "BaseConnection.h"

class Client : public BaseConnection {
private:
    std::string m_host;
    std::string m_port;
    std::string m_password;
    tcp::resolver m_resolver;

public:
    Client(boost::asio::io_context& io_context, const std::string& host, const std::string& port, const std::string& password);
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
    void onError(const std::string& error_message) override;
};
